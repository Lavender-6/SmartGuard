# SmartGuard 数据看板（网页端）

接收 ESP32 上报数据并可视化的迷你网站。**Node.js 零依赖**，不用 npm install。

## 启动

```bash
node server.js          # 默认 http://localhost:8080
PORT=3000 node server.js  # 换端口
```

浏览器打开 `http://localhost:8080` 就能看。事件数据存在同目录 `events.jsonl`，重启自动恢复。

## 页面内容

- 顶部：设备在线/离线（15 秒无上报判定离线）
- 四张卡片：系统模式（布防/撤防/报警）、门磁状态、当前温度、最后上报时间
- 温度趋势图：最近 40 个采样点，超过 45℃ 显示红色报警虚线
- 事件记录表：时间 + 事件标签 + 详情，每 2 秒自动刷新

## 设备上报接口

### POST /api/event（JSON，推荐）

```json
{"type":"door_unlocked","temp":24.6,"door":"closed","mode":"disarmed"}
```

### POST /api/event（纯文本单行，兼容 ESP8266 TCP 透传）

```
armed
disarmed
door_unlocked
ALARM:door_open
ALARM:temp_high
```

### GET /api/status

返回当前状态 + 最近 100 条事件（看板页就在用这个）。

### 事件类型 → 状态映射

| type | 页面显示 | 状态影响 |
|------|---------|---------|
| armed | 布防 | 模式→布防 |
| disarmed | 撤防 | 模式→撤防 |
| door_unlocked | 密码开锁 | 无 |
| door_open / door_closed | 门被打开 / 门已关闭 | 门状态 |
| ALARM:door_open | 报警：门被打开 | 模式→报警 |
| ALARM:temp_high | 报警：温度过高 | 模式→报警 |

## ESP32 端接入（板载 WiFi，稍后固件里加）

```cpp
#include <HTTPClient.h>

void postEvent(const char *type, float temp) {
    HTTPClient http;
    http.begin("http://192.168.1.100:8080/api/event");   // 改成你电脑的 IP
    http.addHeader("Content-Type", "application/json");
    String body = String("{\"type\":\"") + type + "\",\"temp\":" + temp + "}";
    http.POST(body);
    http.end();
}
```

## 注意

- 端口 8080 别和 `wifi_config.h` 里 `TCP_PORT`（ESP8266 透传演示）同时用，二选一或改端口。
- 电脑防火墙如果拦了 Node，首次运行弹窗要点“允许”。
