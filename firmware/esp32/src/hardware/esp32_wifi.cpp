#include "esp32_wifi.h"
#include <WiFi.h>
#include "wifi_config.h"
#include <cstring>

void Esp32Wifi::begin(const char *ssid, const char *pass, uint32_t timeoutMs) {
    strncpy(_ssid, ssid, sizeof(_ssid) - 1);
    strncpy(_pass, pass, sizeof(_pass) - 1);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(_ssid, _pass);

    Serial.printf("[wifi] connecting to \"%s\" ...\n", _ssid);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(250);
        Serial.print('.');
    }
    Serial.println();

    if (isConnected()) {
        Serial.printf("[wifi] OK  ip=%s  rssi=%d\n", ip().c_str(), rssi());
    } else {
        Serial.println("[wifi] connect timeout, background retry enabled");
    }

    _server.on("/", [this]() { handleRoot(); });
    _server.on("/json", [this]() { handleJson(); });
    _server.begin();
    Serial.println("[wifi] web server on :80 (http://" + ip() + "/)");
}

void Esp32Wifi::loop() {
    _server.handleClient();

    // 掉线重连（每 5s 尝试一次，不阻塞）
    if (!isConnected() && millis() - _lastRetry > 5000) {
        _lastRetry = millis();
        Serial.println("[wifi] link down, reconnecting...");
        WiFi.disconnect();
        WiFi.begin(_ssid, _pass);
    }
}

bool Esp32Wifi::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

String Esp32Wifi::ip() const {
    return WiFi.localIP().toString();
}

int Esp32Wifi::rssi() const {
    return (int)WiFi.RSSI();
}

// ---------- 向 Web 看板上报事件 ----------
// 关键：**每个事件都带上当前的真实 mode**。
//   看板只把 ESP32 上报的 mode 当作真相（STM32 的 cmd 一律不改 mode），
//   所以只要这里每次都带 mode，看板 /api/mode 就永远是准的，
//   STM32 那边问到的权限也就永远是对的。
bool Esp32Wifi::postEvent(const char *type, float temp, const char *door, const char *mode) {
    if (!isConnected()) return false;

    // mode 为空时兜底成 "unknown"，避免看板把它当成"没提 mode"而保留旧值
    const char *m = (mode && mode[0]) ? mode : "unknown";

    String url = String("http://") + WEB_HOST + ":" + String(WEB_PORT) + "/api/event";
    String body = String("{\"type\":\"") + type + "\"";
    if (!isnan(temp)) body += String(",\"temp\":") + String(temp, 1);
    if (door) body += String(",\"door\":\"") + door + "\"";
    body += String(",\"mode\":\"") + m + "\"";
    body += "}";

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(3000);
    int code = http.POST(body);
    http.end();

    if (code == 200) {
        Serial.printf("[post] %s -> OK\n", type);
        return true;
    }
    Serial.printf("[post] %s -> HTTP %d\n", type, code);
    return false;
}

// ---------- 取网页下发的门锁命令 ----------
// 返回 action 原文：open / close / emergency / clear_emergency / arm / disarm
//                  ""（连上了但没命令）、"err"（请求失败，调用方可退避）
String Esp32Wifi::pollCommand() {
    if (!isConnected()) return "err";

    String url = String("http://") + WEB_HOST + ":" + String(WEB_PORT) + "/api/cmd";
    HTTPClient http;
    http.begin(url);
    http.setTimeout(2000);
    int code = http.GET();
    String out = "err";
    if (code == 200) {
        String body = http.getString();
        out = "";   // 请求成功，先按"无命令"处理
        int i = body.indexOf("\"action\":\"");
        if (i >= 0) {
            int s = i + 10;                        // 跳过 "action":"
            int e = body.indexOf('"', s);
            if (e > s) out = body.substring(s, e);
        }
    }
    http.end();
    return out;
}

// ---------- 状态网页 ----------
void Esp32Wifi::handleRoot() {
    String html;
    html.reserve(1200);
    html += F("<!DOCTYPE html><html><head><meta charset='utf-8'>");
    html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>SmartGuard</title></head>");
    html += F("<body style='font-family:sans-serif;max-width:520px;margin:24px auto'>");
    html += F("<h2>SmartGuard · ESP32-S3</h2>");
    html += F("<table border='1' cellpadding='8' style='border-collapse:collapse'>");
    html += F("<tr><td>WiFi 状态</td><td>");
    html += isConnected() ? "已连接" : "未连接（重试中）";
    html += F("</td></tr><tr><td>IP 地址</td><td>");
    html += isConnected() ? ip() : "-";
    html += F("</td></tr><tr><td>信号强度</td><td>");
    html += isConnected() ? String(rssi()) + " dBm" : "-";
    html += F("</td></tr><tr><td>运行时间</td><td>");
    html += String(millis() / 1000) + " s";
    html += F("</td></tr><tr><td>空闲内存</td><td>");
    html += String(ESP.getFreeHeap()) + " B";
    html += F("</td></tr></table>");
    html += F("<p>JSON 接口：<a href='/json'>/json</a></p>");
    html += F("</body></html>");
    _server.send(200, "text/html", html);
}

void Esp32Wifi::handleJson() {
    String json = String("{\"wifi\":") + (isConnected() ? "true" : "false") +
                  ",\"ip\":\"" + (isConnected() ? ip() : String("")) +
                  "\",\"rssi\":" + (isConnected() ? String(rssi()) : String("0")) +
                  ",\"uptime_s\":" + String(millis() / 1000) +
                  ",\"free_heap\":" + String(ESP.getFreeHeap()) + "}";
    _server.send(200, "application/json", json);
}
