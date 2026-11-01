/* ============================================================================
 *  esp8266.h —— ESP8266 (ESP-01S) AT 指令驱动
 *
 *  硬件：USART2（PA2=TX→模块RXD，PA3=RX←模块TXD），CH_PD=PB0，RST=PB1
 *  特性：阻塞式 AT 收发 + 超时；WiFi 连接；TCP 连接；HTTP GET 上报
 *
 *  用法：
 *      esp8266_init();                       // 上电、AT 握手
 *      esp8266_join_ap("SSID", "PASS");      // 连 WiFi
 *      esp8266_tcp_open("192.168.43.27", 8080);
 *      esp8266_http_post("/api/event", json);
 * ============================================================================ */
#ifndef ESP8266_H
#define ESP8266_H

#include <stdint.h>

/** 初始化：拉高 CH_PD、复位模块、AT 握手、关闭回显、设 STA 模式。返回 1 成功 */
int  esp8266_init(void);

/** 连接路由器。返回 1 成功 */
int  esp8266_join_ap(const char *ssid, const char *pass);

/** 连接路由器，并指定目标 AP 的 MAC 锁定（附近有同名热点时用）。
 *  bssid 传 NULL 或 "" 等价于 esp8266_join_ap()。返回 1 成功 */
int  esp8266_join_ap_bssid(const char *ssid, const char *pass, const char *bssid);

/** 断开并重连（掉线时用） */
int  esp8266_reconnect(void);

/** 获取本机 IP，写进 buf。返回 1 成功 */
int  esp8266_get_ip(char *buf, uint32_t bufLen);

/** 用 AT+PING 测网络通不通。返回 1 通，0 不通 */
int  esp8266_ping(const char *host);

/** 建立 TCP 连接。返回 1 成功 */
int  esp8266_tcp_open(const char *host, uint16_t port);
void esp8266_tcp_close(void);

/** 单次 TCP 连接 + 发 HTTP POST + 关闭（简单可靠，适合低频上报）。
 *  path 形如 "/api/event"，json 为请求体。返回 1 表示收到 200 */
int  esp8266_http_post(const char *host, uint16_t port,
                       const char *path, const char *json);

/** GET /api/mode 并解析出设备当前模式（armed / disarmed / emergency）。
 *  用于同步 ESP32 的紧急状态（精简接口，响应只有几十字节）。
 *  返回 1 = 成功写入 modeBuf */
int  esp8266_http_get_mode(const char *host, uint16_t port,
                           char *modeBuf, uint32_t modeBufLen);

/** 发送裸 AT 指令并等 "OK"（调试用）。返回 1 收到 OK */
int  esp8266_at(const char *cmd, uint32_t timeoutMs);

/** 最近一次错误描述（可直接 printf） */
const char *esp8266_last_error(void);

/** 诊断：打印最近一次 AT 的原始响应（不可打印字符显示为 '.'） */
void esp8266_dump_resp(void);

/** 诊断：查询并打印当前连接的 AP 和本机 IP */
void esp8266_diag_status(void);

#endif
