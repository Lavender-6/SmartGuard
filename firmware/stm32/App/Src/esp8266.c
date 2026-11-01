/* ============================================================================
 *  esp8266.c —— ESP8266 AT 指令驱动（轮询式，不阻塞系统太久）
 *
 *  设计取舍：初始工程以"能跑通、好读、好改"为优先，全部用阻塞 + 超时，
 *  不用中断状态机。单次 AT 最长等 2s，HTTP 上报最长等 3s，主循环完全够用。
 *  以后要接 FreeRTOS 再换成"中断收 + 队列发"的实现。
 * ============================================================================ */
#include "esp8266.h"
#include "usart.h"
#include "board.h"
#include <string.h>
#include <stdio.h>

/* ---------------------------- 全局状态 ---------------------------- */
static char     g_ssid[33] = {0};
static char     g_pass[65] = {0};
static char     g_bssid[18] = {0};   /* 目标 AP 的 MAC，空串表示不锁定 */
static char     g_err[64]  = "none";
static uint8_t  g_tcpOpen  = 0;

/* 接收缓冲：AT 响应按 1KB 算（CWLAP 扫描结果很长，512 不够用） */
static char     resp[1024];
static uint16_t respLen = 0;

static void set_err(const char *msg) {
    strncpy(g_err, msg, sizeof(g_err) - 1);
    g_err[sizeof(g_err) - 1] = 0;
}

const char *esp8266_last_error(void) { return g_err; }

/* 前置声明：下面的诊断函数要用到本文件后部的内部工具 */
static void resp_reset(void);
static void resp_pump(void);
static void send_line(const char *s);

/** 把最近一次 AT 的原始响应转发到调试口（诊断用；不可打印字符替换成 '.'） */
void esp8266_dump_resp(void) {
    usart_write_n(USART1, "\r\n---- module resp ----\r\n", 25);
    for (uint16_t i = 0; i < respLen; i++) {
        char c = resp[i];
        if (c == '\r') continue;
        if (c == '\n') usart_write_n(USART1, "\r\n", 2);
        else if (c < 0x20 || c > 0x7E) usart_putc(USART1, '.');   /* 中文 SSID 会变点 */
        else usart_putc(USART1, c);
    }
    usart_write_n(USART1, "\r\n---- end ----\r\n", 17);
}

/** 当前连接状态一行：已连的 AP 名 + 本机 IP（诊断用） */
void esp8266_diag_status(void) {
    resp_reset();
    send_line("AT+CWJAP?");
    delay_ms(500);
    resp_pump();
    usart_write_n(USART1, "\r\n[diag] CWJAP?: ", 17);
    esp8266_dump_resp();

    resp_reset();
    send_line("AT+CIPSTA?");
    delay_ms(500);
    resp_pump();
    usart_write_n(USART1, "[diag] CIPSTA?: ", 16);
    esp8266_dump_resp();
}

/* ---------------------------- 收数据 ---------------------------- */
static void resp_reset(void) {
    respLen = 0;
    resp[0] = 0;
    usart2_flush_rx();
}

/** 把环形缓冲里的字节搬进 resp，超过容量就丢弃多余（保留前 511 字节） */
static void resp_pump(void) {
    int c;
    while ((c = usart2_read_byte()) >= 0) {
        if (respLen < sizeof(resp) - 1) {
            resp[respLen++] = (char)c;
            resp[respLen] = 0;
        }
    }
}

/** 在已收到内容里找子串（从 from 开始） */
static int resp_contains(const char *needle, uint16_t from) {
    if (from >= respLen) return 0;
    return strstr(resp + from, needle) != 0;
}

/* ---------------------------- 发数据 ---------------------------- */
static void send_raw(const char *s) {
    usart_write(ESP_UART, s);
}

static void send_line(const char *s) {
    send_raw(s);
    send_raw("\r\n");
}

/**
 * 发指令并等关键字出现。
 * 返回 1 = 等到 expect；0 = 超时
 */
static int wait_for(const char *expect, uint32_t timeoutMs) {
    uint32_t start = 0;
    const uint32_t step = 1;      /* 每次 delay 1ms */
    while (start < timeoutMs) {
        resp_pump();
        if (resp_contains(expect, 0)) return 1;
        if (resp_contains("\r\nERROR", 0))                       /* 模块明确报错 */
            if (!strcmp(expect, "OK")) { set_err("AT ERROR"); return 0; }
        delay_ms(step);
        start += step;
    }
    return 0;
}

/** 发一条 AT 指令，等 OK。返回 1 成功 */
int esp8266_at(const char *cmd, uint32_t timeoutMs) {
    resp_reset();
    send_line(cmd);
    if (wait_for("OK", timeoutMs)) return 1;
    set_err(cmd);
    return 0;
}

/* ---------------------------- 初始化 ---------------------------- */
static void esp_hw_reset(void) {
    /* CH_PD 拉高使能，RST 拉低 100ms 再拉高完成硬复位 */
    gpio_write(ESP_EN_PORT, ESP_EN_PIN, 1);
    gpio_write(ESP_RST_PORT, ESP_RST_PIN, 0);
    delay_ms(100);
    gpio_write(ESP_RST_PORT, ESP_RST_PIN, 1);
    delay_ms(800);            /* 模块启动 + 打印乱码，等它稳定 */
}

int esp8266_init(void) {
    /* 引脚：CH_PD / RST 都是推挽输出 */
    gpio_clock_enable(ESP_EN_PORT);
    gpio_out_pp(ESP_EN_PORT, ESP_EN_PIN);
    gpio_out_pp(ESP_RST_PORT, ESP_RST_PIN);
    gpio_write(ESP_EN_PORT, ESP_EN_PIN, 0);
    gpio_write(ESP_RST_PORT, ESP_RST_PIN, 1);

    usart2_init(115200);      /* ESP-01S 出厂默认 115200 */
    esp_hw_reset();

    /* AT 握手：多波特率探测。
     * 市面 ESP8266 固件默认波特率可能是 115200 / 9600 / 74880。
     * 注意：模块会把上次的 CWJAP 存进 flash，开机自动重连，期间回 "busy p..."，
     * 这时候 AT 发过去没响应 —— 看到 busy 就等它忙完再问，别急着放弃。 */
    static const uint32_t bauds[] = {115200, 9600, 74880};
    int ok = 0;
    for (int b = 0; b < 3 && !ok; b++) {
        usart2_init(bauds[b]);
        delay_ms(50);
        /* 最多试 8 次 × 1s：上电自动重连热点时模块可能忙 3~5 秒 */
        for (int i = 0; i < 8 && !ok; i++) {
            resp_reset();
            send_line("AT");
            uint32_t waited = 0;
            while (waited < 1000) {
                resp_pump();
                if (resp_contains("OK", 0)) { ok = 1; break; }
                if (resp_contains("busy", 0)) {          /* 模块在忙，等它忙完 */
                    delay_ms(500);
                    waited += 500;
                    continue;
                }
                delay_ms(10);
                waited += 10;
            }
            if (!ok) delay_ms(300);
        }
        if (!ok) {
            /* 不是这个波特率 → 让模块切到 115200，下一个循环锁定 */
            resp_reset();
            char cmd[48];
            snprintf(cmd, sizeof(cmd), "AT+UART_CUR=115200,8,1,0,0");
            send_line(cmd);
            delay_ms(300);
        }
    }
    if (!ok) { set_err("no AT response"); return 0; }

    esp8266_at("ATE0", 500);              /* 关回显，后面解析更干净 */
    esp8266_at("AT+CWMODE=1", 500);       /* Station 模式 */
    esp8266_at("AT+CIPMUX=0", 500);       /* 单连接模式（好写） */
    esp8266_at("AT+CIPDINFO=0", 500);     /* 关闭额外连接信息（部分固件不支持，忽略结果） */
    return 1;
}

/* ---------------------------- 连 WiFi ---------------------------- */
int esp8266_join_ap(const char *ssid, const char *pass) {
    return esp8266_join_ap_bssid(ssid, pass, 0);
}

/**
 * 连 AP，可指定 BSSID（目标路由器 MAC）锁定具体那个热点。
 * AT+CWJAP 格式：<ssid>,<pwd>[,<bssid>[,<extssid_flag>]]
 * 给 bssid = NULL 或 "" 就只有两段参数。
 */
int esp8266_join_ap_bssid(const char *ssid, const char *pass, const char *bssid) {
    strncpy(g_ssid, ssid, sizeof(g_ssid) - 1);
    strncpy(g_pass, pass, sizeof(g_pass) - 1);
    /* 记住 BSSID，掉线重连时沿用 */
    g_bssid[0] = 0;
    if (bssid && bssid[0]) strncpy(g_bssid, bssid, sizeof(g_bssid) - 1);

    char cmd[160];
    if (bssid && bssid[0]) {
        /* 三段参数时密码不能省，必须是 "" 形式 */
        snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\",\"%s\"",
                 ssid, pass ? pass : "", bssid);
    } else if (pass && pass[0]) {
        snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, pass);
    } else {
        snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\"", ssid);   /* 开放网络 */
    }

    resp_reset();
    send_line(cmd);

    /* 连接过程可能持续 20s 以上；同时监控 WIFI GOT IP / OK / FAIL / +CWJAP:<n> */
    uint32_t waited = 0;
    while (waited < 25000) {
        resp_pump();
        if (strstr(resp, "WIFI GOT IP")) return 1;
        if (strstr(resp, "OK"))          return 1;
        if (strstr(resp, "FAIL") || strstr(resp, "+CWJAP:")) break;
        delay_ms(10);
        waited += 10;
    }

    /* 失败：把模块返回的错误码打出来，方便定位 */
    char *p = strstr(resp, "+CWJAP:");
    if (p) {
        /* +CWJAP:<error>  1=连接超时 2=密码错 3=没有这个SSID 4=连接失败 */
        snprintf(g_err, sizeof(g_err), "CWJAP err %.20s", p + 7);
    } else if (strstr(resp, "FAIL")) {
        /* 部分固件回 "FAIL" / "ERROR"，把整段响应带出来 */
        snprintf(g_err, sizeof(g_err), "join FAIL(%uB) %.40s", respLen, resp);
    } else {
        snprintf(g_err, sizeof(g_err), "join timeout, %u bytes", respLen);
    }
    return 0;
}

int esp8266_reconnect(void) {
    esp8266_at("AT+CWQAP", 1000);                 /* 先断开 */
    return esp8266_join_ap_bssid(g_ssid, g_pass, g_bssid);
}

int esp8266_get_ip(char *buf, uint32_t bufLen) {
    resp_reset();
    send_line("AT+CIPSTA?");
    if (!wait_for("OK", 1000)) { set_err("CIPSTA fail"); return 0; }

    /* 响应形如：+CIPSTA:ip:"192.168.43.123" */
    char *p = strstr(resp, "ip:\"");
    if (!p) { set_err("no ip"); return 0; }
    p += 4;
    char *q = strchr(p, '"');
    if (!q) { set_err("bad ip fmt"); return 0; }

    uint32_t n = (uint32_t)(q - p);
    if (n >= bufLen) n = bufLen - 1;
    memcpy(buf, p, n);
    buf[n] = 0;
    return 1;
}

int esp8266_ping(const char *host) {
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "AT+PING=\"%s\"", host);
    resp_reset();
    send_line(cmd);
    /* 通 → "+<ms>"；不通 → ERROR */
    return wait_for("OK", 3000) && !strstr(resp, "ERROR");
}

/* ---------------------------- TCP ---------------------------- */
/**
 * 建立单连接 TCP。
 *
 * 原实现有个坑：
 *     wait_for("CONNECT", 5000) || wait_for("ALREADY CONNECTED", 500)
 * 模块回 "ERROR" / "busy p..." 时，第一个 wait_for 找不到 "CONNECT" 也认不出
 * 错误，就得**硬等满 5 秒**。主循环一次同步卡 5s，按键扫描全停 —— 观感就是
 * "按了没反应"。现在改成一次轮询同时匹配多个关键字，失败信号立即返回。
 */
int esp8266_tcp_open(const char *host, uint16_t port) {
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u", host, port);
    resp_reset();
    send_line(cmd);

    uint32_t waited = 0;
    while (waited < 5000) {
        resp_pump();
        if (strstr(resp, "CONNECT")) {          /* 含 ALREADY CONNECTED */
            g_tcpOpen = 1;
            return 1;
        }
        if (strstr(resp, "\r\nERROR") || strstr(resp, "busy p")) {
            break;                               /* 立刻放弃，不再空等 */
        }
        delay_ms(10);
        waited += 10;
    }

    set_err("tcp open failed");
    g_tcpOpen = 0;
    /* 连接状态不明时清一次场，避免后面 CIPSEND 莫名失败 */
    esp8266_at("AT+CIPCLOSE", 300);
    return 0;
}

void esp8266_tcp_close(void) {
    if (g_tcpOpen) {
        esp8266_at("AT+CIPCLOSE", 1000);
        g_tcpOpen = 0;
    }
}

int esp8266_http_post(const char *host, uint16_t port,
                      const char *path, const char *json) {
    if (!esp8266_tcp_open(host, port)) return 0;

    uint32_t bodyLen = (uint32_t)strlen(json);
    /* 请求报文：头部 + 空行 + body */
    char header[192];
    snprintf(header, sizeof(header),
             "POST %s HTTP/1.1\r\n"
             "Host: %s:%u\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %lu\r\n"
             "Connection: close\r\n\r\n",
             path, host, port, (unsigned long)bodyLen);
    uint32_t total = (uint32_t)strlen(header) + bodyLen;

    /* 1) 告诉模块要发多少字节 */
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%lu", (unsigned long)total);
    resp_reset();
    send_line(cmd);
    if (!wait_for(">", 2000)) {          /* 收到 '>' 才能发数据 */
        set_err("CIPSEND no prompt");
        esp8266_tcp_close();
        return 0;
    }

    /* 2) 发数据（这里不要等 OK，直接灌进去） */
    send_raw(header);
    send_raw(json);

    /* 3) 等服务器响应里的 200 */
    resp_reset();
    int ok = wait_for("HTTP/1.1 200", 5000);
    if (!ok) set_err("http no 200");

    esp8266_tcp_close();
    return ok;
}

/* ---------------------------- HTTP GET：查门锁权限 ----------------------------
 * 请求精简接口 GET /api/mode?device=stm32，响应只有一个短 JSON：
 *     {"mode":"disarmed","esp":"disarmed","door":"closed","ts":1793448157379}
 *
 * 为什么要带 device=stm32：
 *   看板会按"请求方身份"折算一份有效模式。对 STM32 来说
 *     emergency  ESP32 在紧急态 → 绝对不许动门锁
 *     armed      ESP32 在布防   → 短按开门会被拒（属于闯空门）
 *     stm32      ESP32 掉线     → 门锁交给本机控制器（合理的降级）
 *     disarmed   自由
 *
 * 为什么不用 /api/status：
 *   那个响应带 100 条事件，远超 resp[1024]，解析必然失败。
 *
 * 返回 1 = 取到了模式（写进 modeBuf），0 = 失败
 */
int esp8266_http_get_mode(const char *host, uint16_t port,
                          char *modeBuf, uint32_t modeBufLen) {
    if (!esp8266_tcp_open(host, port)) return 0;

    char req[192];
    snprintf(req, sizeof(req),
             "GET /api/mode?device=stm32 HTTP/1.1\r\n"
             "Host: %s:%u\r\n"
             "Connection: close\r\n\r\n",
             host, port);

    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%lu", (unsigned long)strlen(req));
    resp_reset();
    send_line(cmd);
    if (!wait_for(">", 2000)) {
        set_err("CIPSEND no prompt");
        esp8266_tcp_close();
        return 0;
    }
    send_raw(req);

    /* 响应很短，一次 IPD 基本就全了；等出现 "mode" 后再收一小会儿 */
    resp_reset();
    uint32_t waited = 0;
    while (waited < 3000) {
        resp_pump();
        if (strstr(resp, "\"mode\"")) {
            delay_ms(120);
            resp_pump();
            break;
        }
        delay_ms(10);
        waited += 10;
    }
    /* [修复] 必须先解析、后关连接：tcp_close 内部 esp8266_at() 会 resp_reset()
     * 把 resp 清空——先 close 再解析，resp 里什么都没有，解析必然失败。
     * 这就是"POST 正常、GET 必败"的根因：POST 在 wait_for 里边收边匹配，
     * 不依赖 close 之后的缓冲；GET 却在 close 之后才 strstr。 */
    char *p = strstr(resp, "\"mode\":\"");
    if (!p && !strstr(resp, "\"mode\"")) {
        /* [diag] 确实没收到 mode 时 dump 原始响应（正常情况不会触发） */
        char dbg[64];
        snprintf(dbg, sizeof(dbg), "\r\n[get] WAIT-END respLen=%u raw:\r\n", (unsigned)respLen);
        usart_write(USART1, dbg);
        esp8266_dump_resp();
    }
    esp8266_tcp_close();
    if (!p) { set_err("no mode in resp"); return 0; }
    p += 8;
    char *q = strchr(p, '"');
    if (!q) { set_err("bad mode fmt"); return 0; }

    uint32_t n = (uint32_t)(q - p);
    if (n >= modeBufLen) n = modeBufLen - 1;
    memcpy(modeBuf, p, n);
    modeBuf[n] = 0;
    return 1;
}
