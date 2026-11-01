/* ============================================================================
 *  main.c —— STM32F103C8T6 + ESP8266 + 按键 初始工程
 *
 *  上电流程：
 *    1. SystemInit()：时钟 72MHz（启动文件里已调）
 *    2. USART1 初始化（115200，用来 printf 看日志）
 *    3. 板载 LED 心跳 + 按键 + ESP8266 初始化
 *    4. 连 WiFi → 打印 IP → PING 一下网关/服务器
 *    5. 进入主循环：10ms 轮询按键，按一下往网页发一条事件
 *
 *  按键功能：
 *    短按 → 上报一条 "key_press" 事件到网页（验证全链路通）
 *    长按 1s → 重连 WiFi
 *
 *  ⚠️ 先改 App/Inc/wifi_config.h 里的 SSID / 密码 / 服务器 IP
 * ============================================================================ */
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "usart.h"
#include "key.h"
#include "esp8266.h"
#include "wifi_config.h"
/* ------------------------- 小工具：LED ------------------------- */
static void led_init(void) {
    gpio_clock_enable(LED_PORT);
    gpio_out_pp(LED_PORT, LED_PIN);
    gpio_write(LED_PORT, LED_PIN, 1);      /* 低电平点亮，先关掉 */
}

/* ------------------------- 小工具：上报事件 ------------------------- */
/**
 * 往网页看板发一条事件。
 *   type  事件类型：boot / key_press / cmd
 *   extra 附加的 JSON 片段（不含首尾逗号），可为 NULL
 * 所有事件都带 "device":"stm32"，看板据此区分 ESP32 与 STM32。
 */
static void post_event(const char *type, const char *extra) {
    char body[192];
    if (extra && extra[0])
        snprintf(body, sizeof(body),
                 "{\"device\":\"stm32\",\"type\":\"%s\",%s}", type, extra);
    else
        snprintf(body, sizeof(body),
                 "{\"device\":\"stm32\",\"type\":\"%s\"}", type);

    if (esp8266_http_post(WEB_HOST, WEB_PORT, "/api/event", body)) {
        printf("[net] POST %s -> OK\r\n", type);
    } else {
        printf("[net] POST %s -> FAIL (%s)\r\n", type, esp8266_last_error());
    }
}

/**
 * 向下游（ESP32 舵机）发指令。
 *   action = "open" / "close" / "emergency"
 *   src    = 指令来源，写进看板事件详情，方便追责："stm32-key" / "stm32-emergency"
 */
static void send_lock_cmd(const char *action, const char *src) {
    char extra[128];
    snprintf(extra, sizeof(extra), "\"action\":\"%s\",\"src\":\"%s\"", action, src);
    post_event("cmd", extra);
}

/* ------------------------- 门锁状态（本地记忆） -------------------------
 * STM32 现在要"按一下切换一次"，所以必须自己记住当前门是开还是关。
 * ESP32 那边不回报开锁状态（door_unlocked 事件里没有门锁字段），
 * 因此以本地状态为准：开机当作"已关门"。 */
static int door_is_open = 0;            /* 0 = 关着  1 = 开着 */

/* ---------------------- 门锁控制权（取代旧的 emergency_latched） ----------------------
 *
 * 【上一版的坑】旧代码用一个 emergency_latched 布尔锁在本地，靠"猜测"判断能不能
 * 按键开门，结果两条路都进死胡同：
 *   1) 本地置位、但同步请求一直失败（看板没有 /api/mode 接口 → 404；
 *      或 /api/status 响应太大解析不出来）→ 锁永远清不掉 → 永远"按了没反应"。
 *   2) 加了"连续失败 3 次就解锁"的兜底 → 又变成看板一挂就能绕过紧急状态开门。
 *
 * 【现在的规则】权限**一律由看板裁决**，本机不再做任何推断。
 *   每次短按都先问看板 GET /api/mode?device=stm32，返回：
 *     "emergency" → ESP32 在紧急态，拒绝（唯一解除途径：ESP 端输密码）
 *     "armed"     → ESP32 在布防，拒绝（短按开门等于闯空门）
 *     "stm32"     → ESP32 掉线，门锁交给本机控制器，放行
 *     "disarmed"  → 自由，放行
 *     ""（问不到）→ 保守拒绝（宁可不开，也不误解锁）
 *   好处：紧急状态什么时候解除完全由 ESP32 说了算，本机没有任何"锁存"可卡住。
 *
 * 代价：按一下要多花一次 HTTP（约 100~300ms），可以接受。
 * 另外每 10s 也会同步一次，纯粹为了 LED / 串口日志能显示当前权限，
 * 不影响按键逻辑。
 */
static int lock_mode_synced = 0;        /* 1 = 已成功问过看板（仅用于日志/显示） */
static char lock_mode[16] = {0};        /* 最近一次问到的模式 */
static int modeFailCnt = 0;             /* 连续取模式失败次数 */

/* ------------------------- 连网流程 ------------------------- */
static int online = 0;                  /* 1 = WiFi 已连上（主循环里做健康检查） */

/* ---------------- 向看板要一份"当前能不能动门锁"的裁决 ----------------
 * 返回 1 = 放行，0 = 拒绝（含"问不到"的情况）
 * 结果同时写进 lock_mode，供日志和 LED 显示用
 */
static int ask_door_permission(void) {
    char mode[16] = {0};
    if (!esp8266_http_get_mode(WEB_HOST, WEB_PORT, mode, sizeof(mode))) {
        printf("[lock] permission DENIED (mode query failed: %s)\r\n",
               esp8266_last_error());
        return 0;                        /* 问不到就不开，保守处理 */
    }
    strncpy(lock_mode, mode, sizeof(lock_mode) - 1);
    lock_mode[sizeof(lock_mode) - 1] = 0;
    lock_mode_synced = 1;
    modeFailCnt = 0;

    if (strcmp(mode, "emergency") == 0) {
        printf("[lock] permission DENIED: EMERGENCY (enter password on ESP32)\r\n");
        return 0;
    }
    if (strcmp(mode, "armed") == 0) {
        printf("[lock] permission DENIED: ESP32 is ARMED (disarm first)\r\n");
        return 0;
    }
    printf("[lock] permission OK (mode=%s)\r\n", mode);
    return 1;                            /* disarmed / stm32 */
}

/* ======================= 扫描诊断模式 =======================
 * 把 SCAN_ONLY 打开，固件就只做一件事：让 ESP8266 扫一遍周围 2.4G 热点，
 * 按信号强弱列出来，然后停住不动。
 *
 * 判断依据：
 *   - 列表里出现 "123"      → 热点没问题，是连接阶段的事
 *   - 列表里没有 "123"      → 电脑移动热点对 ESP8266 不可见（常见于 Intel 网卡软 AP）
 *   - 列表是空的 / 只有 ERROR → 天线或供电问题
 * ================================================================= */
#define SCAN_ONLY   0

static void scan_and_list(void) {
    int total = 0;                        /* 两次扫描里 AP 的最大数量 */
    /* 模块可能还在忙上电自动重连，扫不到就等 3s 再扫一次 */
    for (int attempt = 1; attempt <= 2; attempt++) {
        char line[128];
        int  n = 0;
        uint32_t waited = 0;
        total = 0;
        printf("\r\n[scan] AT+CWLAP (attempt %d) ...\r\n", attempt);

        usart_write(ESP_UART, "AT+CWMODE=1\r\n");
        delay_ms(600);
        usart2_flush_rx();

        usart_write(ESP_UART, "AT+CWLAP\r\n");

        /* 边收边解析，按行输出，最多等 12s（AP 多的时候扫描慢） */
        while (waited < 12000) {
        int c = usart2_read_byte();
        if (c < 0) { delay_ms(1); waited++; continue; }

        if (c == '\r') continue;
        if (c == '\n' || n >= (int)sizeof(line) - 1) {
            line[n] = 0;
            if (n > 0) {
                /* 只关心 +CWLAP: 开头的行，其余（OK / ERROR）也打出来 */
                int printable = 1;
                for (int i = 0; i < n; i++) {
                    if ((unsigned char)line[i] < 0x20 || (unsigned char)line[i] > 0x7E) {
                        printable = 0;
                        break;
                    }
                }
                if (strncmp(line, "+CWLAP:", 7) == 0) total++;
                printf("  %s%s\r\n", printable ? "" : "[非ASCII] ", line);
            }
            n = 0;
            if (strncmp(line, "OK", 2) == 0) break;   /* 扫完了 */
        } else {
            line[n++] = (char)c;
        }
    }

        printf("[scan] done, %d AP(s) found\r\n", total);
        if (total > 0) break;                 /* 扫到了就不用重试 */
        printf("[scan] retry in 3s ...\r\n");
        delay_ms(3000);
    }

    if (total == 0) {
        printf("[scan] 一个热点都没扫到 —— 检查天线(板载/外置) 和 3V3 供电(要 >300mA)\r\n");
    }
    printf("[scan] 现在停住。改 SCAN_ONLY 为 0 重新烧录即可恢复联网流程。\r\n");
}

static int connect_wifi(void) {
    if (!esp8266_init()) {
        printf("[esp] init FAILED: %s\r\n", esp8266_last_error());
        return 0;
    }
    printf("[esp] AT ok\r\n");

    printf("[esp] joining AP: %s", WIFI_SSID);
    if (WIFI_BSSID[0]) printf(" (bssid %s)", WIFI_BSSID);
    printf(" ...\r\n");

    if (!esp8266_join_ap_bssid(WIFI_SSID, WIFI_PASS,
                               WIFI_BSSID[0] ? WIFI_BSSID : 0)) {
        printf("[esp] join FAILED: %s\r\n", esp8266_last_error());
        /* 诊断：模块当前连着谁、分到什么 IP、周围扫到哪些 AP */
        esp8266_diag_status();
        usart2_flush_rx();            /* 清掉 join 的残留响应 */
        usart_write(ESP_UART, "AT+CWLAP\r\n");
        {
            uint32_t t = 0;
            while (t < 6000) {                    /* 最多等 6s 收扫描结果 */
                int c = usart2_read_byte();
                if (c >= 0) {
                    if (c == '\r') continue;
                    char ch = (char)c;
                    if (ch == '\n') usart_write_n(USART1, "\r\n", 2);
                    else if (ch < 0x20 || ch > 0x7E) usart_putc(USART1, '.');
                    else usart_putc(USART1, ch);
                } else { delay_ms(1); t++; }
            }
            usart_write_n(USART1, "\r\n", 2);
        }
        return 0;
    }

    char ip[20] = {0};
    if (esp8266_get_ip(ip, sizeof(ip))) printf("[esp] local ip = %s\r\n", ip);
    else                               printf("[esp] get ip failed\r\n");

    printf("[esp] ping %s ... %s\r\n", WEB_HOST,
           esp8266_ping(WEB_HOST) ? "OK" : "FAIL");
    return 1;
}

/* ============================== main ============================== */
int main(void) {
    usart1_init(115200);                 /* 调试串口，先起来才能看日志 */
    led_init();
    key_init();

    /* 标题只用 ASCII，防止不同终端/编码的乱码干扰判断 */
    printf("\r\n");
    printf("==========================================\r\n");
    printf(" STM32F103C8T6 + ESP8266 demo\r\n");
    printf(" SYSCLK = %lu Hz\r\n", (unsigned long)SystemCoreClock);
    printf("==========================================\r\n");

    /* 上电自检：LED 闪 3 下 */
    for (int i = 0; i < 3; i++) {
        gpio_write(LED_PORT, LED_PIN, 0);
        delay_ms(120);
        gpio_write(LED_PORT, LED_PIN, 1);
        delay_ms(120);
    }

    if (SCAN_ONLY) {
        /* 纯诊断：只初始化模块 + 扫描热点，不去 join */
        if (!esp8266_init()) {
            printf("[esp] init FAILED: %s\r\n", esp8266_last_error());
        } else {
            printf("[esp] AT ok\r\n");
        }
        scan_and_list();
        while (1) {
            gpio_toggle(LED_PORT, LED_PIN);
            delay_ms(500);
        }
    }

    online = connect_wifi();
    if (online) post_event("boot", 0);

    /* 首次连上：LED 直接长亮，不等主循环 */
    if (online) gpio_write(LED_PORT, LED_PIN, 0);

    /* 没连上时每隔 15s 自动重试一次（不用重烧固件） */
    uint32_t lastRetryMs = 0;
    uint32_t nowMs = 0;
    const uint32_t RETRY_PERIOD_MS = 15000;

    /* ------------------------ 主循环 ------------------------ */
    uint32_t tick = 0;
    uint32_t lastHeartbeat = 0;
    uint32_t lastReconnect  = 0;
    uint32_t lastModeSync   = 0;

    while (1) {
        /* 1) 按键：10ms 一次
         *    短按 → 门锁开/关切换（先问看板要权限，紧急/布防态会被拒）
         *    长按 1s → 紧急状态：ESP32 红灯闪 + 强制开门
         *    没联网时不动门锁状态（指令发不出去，状态会跟实际不一致） */
        KeyEvent ev = key_poll_10ms();
        if (ev == KEY_SHORT) {
            if (!online) {
                printf("[key] short press ignored (offline)\r\n");
            } else if (!ask_door_permission()) {
                /* 拒绝理由已由 ask_door_permission 打印（紧急/布防/问不到） */
            } else if (!door_is_open) {
                printf("[key] short press -> door OPEN\r\n");
                send_lock_cmd("open", "stm32-key");
                door_is_open = 1;
            } else {
                printf("[key] short press -> door CLOSE\r\n");
                send_lock_cmd("close", "stm32-key");
                door_is_open = 0;
            }
        } else if (ev == KEY_LONG) {
            printf("[key] long press -> EMERGENCY (force open)\r\n");
            if (online) {
                send_lock_cmd("emergency", "stm32-emergency");
                door_is_open = 1;        /* 紧急会强制开门，本地状态跟着置开 */
                /* 注意：这里**不再**本地置任何锁存位。
                 * 之后能不能按键，每次都由看板裁决；ESP32 输密码解除后，
                 * 下一次短按问到的就是 disarmed，自然恢复。 */
            }
        }

        /* 2) LED 指示（PC13 低电平点亮，0 = 亮）：
         *      未联网         → 500ms 闪一次（正在找网/重试）
         *      联网 + 紧急态   → 100ms 快闪（和 ESP32 红灯对应，一眼看出被锁）
         *      联网 + 其余     → 长亮（网络就绪，门锁权限正常）
         *   如果看板已经很久没问到模式（lock_mode_synced 掉线），也按未联网闪。 */
        if (online && lock_mode_synced) {
            if (strcmp(lock_mode, "emergency") == 0) {
                if (tick - lastHeartbeat >= 10) {         /* 10 × 10ms = 100ms */
                    lastHeartbeat = tick;
                    gpio_toggle(LED_PORT, LED_PIN);
                }
            } else {
                gpio_write(LED_PORT, LED_PIN, 0);         /* 长亮 */
            }
        } else if (tick - lastHeartbeat >= 50) {          /* 50 × 10ms = 500ms */
            lastHeartbeat = tick;
            gpio_toggle(LED_PORT, LED_PIN);
        }

        /* 3) 没上线的自动重试：每 15s 重新初始化模块并连 WiFi
         *    这样插好线/通上电后不用复位，等一会儿自己就通 */
        nowMs = tick * 10;
        if (!online && nowMs - lastRetryMs >= RETRY_PERIOD_MS) {
            lastRetryMs = nowMs;
            printf("[esp] retry init + join ...\r\n");
            online = connect_wifi();
            if (online) post_event("boot", 0);
        }

        /* 4) 掉线自动重连：每 30s 试一次 */
        if (online && tick - lastReconnect >= 3000) {  /* 3000 × 10ms = 30s */
            lastReconnect = tick;
            /* 用 ping 服务器判断是否还通；不通就重连 */
            if (!esp8266_ping(WEB_HOST)) {
                printf("[net] lost, reconnecting...\r\n");
                online = esp8266_reconnect() ? 1 : 0;
            }
        }

        /* 5) 定期同步一次门锁权限：每 10s 问一次看板
         *    这一步**不影响按键判断**（按键每次都单独问），只做两件事：
         *      ① 串口日志 / LED 提示能反映当前权限变化
         *      ② 刷新 lock_mode，方便排查
         *    间隔拉长到 10s，是因为每次 HTTP 会阻塞主循环上百毫秒，
         *    太频繁会让按键手感变钝。
         */
        if (online && tick - lastModeSync >= 1000) {    /* 1000 × 10ms = 10s */
            lastModeSync = tick;
            char mode[16] = {0};
            if (esp8266_http_get_mode(WEB_HOST, WEB_PORT, mode, sizeof(mode))) {
                modeFailCnt = 0;
                /* 模式变了才打印，避免日志刷屏 */
                if (strcmp(mode, lock_mode) != 0) {
                    printf("[sync] lock mode -> %s\r\n", mode);
                    strncpy(lock_mode, mode, sizeof(lock_mode) - 1);
                    lock_mode[sizeof(lock_mode) - 1] = 0;
                }
                lock_mode_synced = 1;
            } else {
                modeFailCnt++;
                if (modeFailCnt == 1 || (modeFailCnt % 6) == 0) {
                    printf("[sync] mode query failed x%d (%s)\r\n",
                           modeFailCnt, esp8266_last_error());
                }
            }
        }

        tick++;
        delay_ms(10);                                /* 主循环节拍：10ms */
    }
}
