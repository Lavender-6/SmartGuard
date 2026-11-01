/* ============================================================================
 *  wifi_config.h —— 路由器 / 服务器参数（改这里就够了）
 * ============================================================================ */
#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

/* ---- 要连的路由器（手机热点也行） ---- */
#define WIFI_SSID   "YOUR_WIFI_SSID"
#define WIFI_PASS   "YOUR_WIFI_PASSWORD"

/* ---- 目标 AP 的 MAC（BSSID），可选 ----
 * 如果附近存在**两个同名热点**（常见于邻居也起了同名热点），直接按名字连有可能连错。
 * 填上 MAC 可以锁定直连，跳过扫描环节。
 * 留空 "" 表示不用 BSSID（只按 SSID 连）。
 * 查自己的热点 MAC：netsh wlan show interfaces 里的"物理地址"
 * 注意：Windows 移动热点是 Wi-Fi Direct 软 AP，其广播的 BSSID 不是网卡物理地址
 *       （首字节的 local 位被置 1，如 b0:47:… → b2:47:…），要以扫描结果为准。 */
#define WIFI_BSSID  ""   /* 可选：热点 MAC，留空则只按 SSID 连 */

/* ---- 电脑上网页看板的地址（与 ESP32 SmartGuard 工程一致） ---- */
#define WEB_HOST    "192.168.137.1"
#define WEB_PORT    8080

#endif
