// ============================================================
//  SmartGuard 数据接收服务（零依赖，Node 直接运行）
//  运行：node server.js        端口：http://localhost:8080
//  接口：
//    POST /api/event   设备上报（JSON 或纯文本行）
//    GET  /api/status  看板数据（当前状态 + 最近事件）
// ============================================================
const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = process.env.PORT || 8080;
const PUBLIC_DIR = path.join(__dirname, 'public');
const LOG_FILE = path.join(__dirname, 'events.jsonl');

// ---- 状态与事件 ----
//  state.mode = ESP32 门禁主控上报的**真实模式**（disarmed / armed / emergency）
//  state.door = 门磁
//  state.lock = 门锁归属：esp = ESP32 全权控制；stm32 = 控制器按键控制
//
//  ⚠️ mode 只能由 ESP32 自己上报的事件改写。
//     STM32 发来的 cmd（open/close/arm/disarm/emergency）绝不能改 mode ——
//     否则"按一下短按开门"就会把 armed 顶成 disarmed、把 emergency 顶掉，
//     两端状态永远对不上，STM32 也就永远退不出紧急锁存。
//     STM32 通过 GET /api/mode?device=stm32 拿到 **有效模式 effMode()**：
//       ESP32 在紧急态          → emergency（谁都不能动门锁）
//       ESP32 在线且布防         → armed（STM32 无权开门）
//       STM32 在线且 ESP32 不在线 → stm32（ESP32 挂了，门锁交给控制器）
//       其余                     → disarmed（自由）
let state = { mode: 'unknown', door: 'unknown', lock: 'disarmed', temp: null, ip: '', lastSeen: 0 };
let events = [];

// 设备心跳：按"哪台设备"分别记录最后活跃时间
//   esp32 = 门禁主控（上报温度/门磁/自带网页）
//   stm32 = 控制器（按键下发门锁指令）
let devSeen = { esp32: 0, stm32: 0 };
let stm32Info = { ip: '', last: 0, cmdCount: 0 };
const ONLINE_MS = 15000;        // 15s 内有上报就算在线

// ---- 远程控制：网页/STM32 下发门锁命令，ESP32 轮询取走 ----
let pendingCmd = '';        // 'open' | 'close' | 'emergency' | ''
let pendingCmdAt = 0;
let pendingSrc = '';        // 'web' | 'stm32-key' | 'stm32-emergency'
const CMD_TTL_MS = 10000;   // 命令 10s 内有效，过期作废（防设备离线后恢复时误开门）
// 合法指令：
//   open / close        普通开关门（紧急状态下设备会拒绝）
//   emergency           进入紧急状态（红灯闪 + 强制开门）
//   clear_emergency     解除紧急状态
//   arm / disarm        布防 / 撤防
const VALID_CMDS = ['open', 'close', 'emergency', 'clear_emergency', 'arm', 'disarm'];
// 紧急指令有效期更长：宁可晚一点执行也不能丢（ESP32 掉线重连后仍能收到）
const EMERGENCY_TTL_MS = 60000;

// 用一条事件更新当前状态（实时和启动回放共用）
function apply(ev) {
    // mode：**只有 ESP32 上报的**才认（cmd 事件来自 STM32，绝不能改 mode）
    const isEsp = String(ev.device || '').toLowerCase() !== 'stm32';
    if (ev.mode && isEsp) state.mode = ev.mode;
    if (ev.door) state.door = ev.door;
    if (ev.lock) state.lock = ev.lock;
    if (typeof ev.temp === 'number' && !isNaN(ev.temp)) state.temp = ev.temp;
    if (ev.ip) state.ip = ev.ip;
    state.lastSeen = ev.t || Date.now();

    // 按设备归类心跳
    const dev = String(ev.device || '').toLowerCase();
    if (dev === 'stm32') {
        devSeen.stm32 = ev.t || Date.now();
        if (ev.ip) stm32Info.ip = ev.ip;
        if (String(ev.type || '').toLowerCase() === 'cmd') stm32Info.cmdCount++;
    } else {
        // 没标 device 的都算 ESP32（老版本固件兼容）
        devSeen.esp32 = ev.t || Date.now();
    }

    const t = String(ev.type || '').toLowerCase();
    // 只有 ESP32 的事件才改模式（STM32 不会发这些 type，双保险）
    if (isEsp) {
        if (t === 'armed') state.mode = 'armed';
        else if (t === 'disarmed') state.mode = 'disarmed';
        else if (t === 'emergency' || t.startsWith('emergency:')) state.mode = 'emergency';
        else if (t.startsWith('alarm')) state.mode = 'emergency';   // 兼容老固件的 ALARM:xxx
    }
    if (t === 'door_open') state.door = 'open';
    else if (t === 'door_closed') state.door = 'closed';
}

// ---- 有效模式：算给特定设备看的那一份 ----
// 为什么要单独算：
//   ① ESP32 一旦进紧急，任何设备都不许动门锁（必须 ESP 端输密码解除）
//   ② ESP32 在布防态时，STM32 按键开门属于"闯空门"，不允许
//   ③ ESP32 掉线时，让 STM32 接管门锁 —— 这是本项目唯一合理的降级方案，
//      否则门禁主控一挂，物理按键就成了废键
//   ④ 其余情况（撤防）自由
function online_() {
    const now = Date.now();
    return {
        esp: now - devSeen.esp32 < ONLINE_MS,
        stm: now - devSeen.stm32 < ONLINE_MS
    };
}

function effMode(forDevice) {
    const on = online_();
    if (state.mode === 'emergency') return 'emergency';
    if (forDevice === 'stm32') {
        if (on.esp) return state.mode === 'armed' ? 'armed' : 'disarmed';
        return on.stm ? 'stm32' : 'disarmed';   // ESP32 不在线 → 交给控制器
    }
    return state.mode === 'armed' ? 'armed' : 'disarmed';
}

// 启动时恢复历史事件（最多 200 条）
try {
    const lines = fs.readFileSync(LOG_FILE, 'utf8').trim().split('\n').filter(Boolean);
    for (const l of lines.slice(-200)) {
        try {
            const ev = JSON.parse(l);
            events.push(ev);
            apply(ev);
        } catch (e) { /* 跳过坏行 */ }
    }
    if (events.length) console.log('[boot] restored', events.length, 'events');
} catch (e) { /* 没有历史文件，正常 */ }

// ---- 上报接口 ----
function handleEvent(req, res) {
    let body = '';
    req.on('data', c => { body += c; if (body.length > 1e6) req.destroy(); });
    req.on('end', () => {
        const ev = { t: Date.now(), ip: (req.socket.remoteAddress || '').replace('::ffff:', '') };
        const ct = (req.headers['content-type'] || '').toLowerCase();
        try {
            if (ct.indexOf('json') >= 0) {
                const j = JSON.parse(body || '{}');
                ev.type = j.type || j.event || 'unknown';
                if (j.mode) ev.mode = j.mode;
                if (j.door) ev.door = j.door;
                if (j.device) ev.device = String(j.device).toLowerCase();
                if (j.action) ev.action = String(j.action).toLowerCase();
                if (j.src) ev.src = String(j.src);          // 指令来源，如 "stm32-key"
                const temp = parseFloat(j.temp);
                if (!isNaN(temp)) ev.temp = temp;
            } else {
                // 纯文本行，例如：armed / disarmed / door_unlocked / ALARM:door_open
                ev.type = String(body).trim().split('\n')[0].trim();
            }
        } catch (e) {
            res.writeHead(400, cors()); return res.end('bad json');
        }
        if (!ev.type) { res.writeHead(400, cors()); return res.end('missing type'); }

        // STM32/设备下发的门锁指令：直接转成 ESP32 要取的命令
        // （type=cmd + action=open|close|emergency → 写入 pendingCmd）
        //
        // ⚠️ 这里**不写 ev.mode**。cmd 是"请求 ESP32 做某事"，不是"ESP32 已经做成"。
        //    以前在这里顺手把 mode 改成 armed/disarmed，导致 STM32 每按一次短按
        //    （发 open/close→被当成 disarmed）就把布防态顶掉，两端永久失步。
        //    现在模式一律等 ESP32 真正执行完、自己上报 armed/disarmed/emergency 才更新。
        let queued = false;
        if (ev.type.toLowerCase() === 'cmd' && VALID_CMDS.indexOf(ev.action) >= 0) {
            pendingCmd = ev.action;
            pendingCmdAt = Date.now();
            pendingSrc = ev.src || (ev.device === 'stm32' ? 'stm32' : 'device');
            queued = true;
            console.log('[cmd] ' + pendingSrc + ' ->', ev.action);
        }

        events.push(ev);
        if (events.length > 500) events = events.slice(-500);
        try { fs.appendFileSync(LOG_FILE, JSON.stringify(ev) + '\n'); } catch (e) {}
        apply(ev);

        console.log('[event]', ev.type, ev.device || '', ev.action || '', ev.temp != null ? ev.temp + 'C' : '', ev.door || '');
        res.writeHead(200, Object.assign({ 'Content-Type': 'application/json' }, cors()));
        res.end(JSON.stringify({ ok: true, queued }));
    });
}

function cors() {
    return {
        'Access-Control-Allow-Origin': '*',
        'Access-Control-Allow-Methods': 'GET,POST,OPTIONS',
        'Access-Control-Allow-Headers': 'Content-Type'
    };
}

// ---- 静态文件 ----
const MIME = { '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.json': 'application/json', '.png': 'image/png', '.svg': 'image/svg+xml' };
function serveStatic(req, res, urlPath) {
    let file = urlPath === '/' ? '/index.html' : urlPath;
    const abs = path.normalize(path.join(PUBLIC_DIR, file));
    if (!abs.startsWith(PUBLIC_DIR)) { res.writeHead(403); return res.end(); }
    fs.readFile(abs, (err, data) => {
        if (err) { res.writeHead(404); return res.end('not found'); }
        res.writeHead(200, Object.assign({ 'Content-Type': MIME[path.extname(abs)] || 'application/octet-stream' }, cors()));
        res.end(data);
    });
}

// ---- 远程控制：网页下发门锁命令，设备轮询取走 ----
function cmdTtl(action) {
    return (action === 'emergency' || action === 'clear_emergency')
        ? EMERGENCY_TTL_MS : CMD_TTL_MS;
}

function handleCmdSet(req, res) {
    let body = '';
    req.on('data', c => { body += c; if (body.length > 1000) req.destroy(); });
    req.on('end', () => {
        let action = '';
        try { action = String(JSON.parse(body || '{}').action || '').toLowerCase(); } catch (e) {}
        if (VALID_CMDS.indexOf(action) < 0) {
            res.writeHead(400, Object.assign({ 'Content-Type': 'application/json' }, cors()));
            return res.end('{"ok":false,"error":"action must be open|close|emergency|clear_emergency|arm|disarm"}');
        }
        pendingCmd = action;
        pendingCmdAt = Date.now();
        pendingSrc = 'web';
        console.log('[cmd] web ->', action);
        res.writeHead(200, Object.assign({ 'Content-Type': 'application/json' }, cors()));
        res.end(JSON.stringify({ ok: true, action }));
    });
}

function handleCmdGet(req, res) {
    let action = '';
    let src = '';
    let age = 0;
    if (pendingCmd && Date.now() - pendingCmdAt < cmdTtl(pendingCmd)) {
        action = pendingCmd;
        src = pendingSrc;
        age = Date.now() - pendingCmdAt;
    }
    pendingCmd = '';   // 一次性消费
    pendingSrc = '';
    res.writeHead(200, Object.assign({ 'Content-Type': 'application/json' }, cors()));
    res.end(JSON.stringify({ action, src, age_ms: age }));
}

// ---- 路由 ----
const server = http.createServer((req, res) => {
    const u = new URL(req.url, 'http://x');
    if (req.method === 'OPTIONS') { res.writeHead(204, cors()); return res.end(); }
    if (req.method === 'POST' && u.pathname === '/api/event') return handleEvent(req, res);
    if (req.method === 'POST' && u.pathname === '/api/cmd') return handleCmdSet(req, res);
    if (req.method === 'GET' && u.pathname === '/api/cmd') return handleCmdGet(req, res);
    // 精简状态接口：专供 STM32 这种内存/缓冲很小的设备轮询
    // 只返回一行短 JSON，避免 /api/status 的几百条事件把设备缓冲打爆
    //
    //   ?device=stm32 → 返回"STM32 视角"的有效模式（见 effMode 注释）
    //   不带参数       → 返回 ESP32 的真实模式（网页/调试用）
    //
    // mode 取值：
    //   emergency  ESP32 在紧急态，任何设备都不能动门锁
    //   armed      ESP32 在布防，STM32 按键开门会被拒
    //   disarmed   自由
    //   stm32      ESP32 掉线，门锁交给 STM32 控制器（唯一合理的降级）
    if (req.method === 'GET' && u.pathname === '/api/mode') {
        const dev = (u.searchParams.get('device') || '').toLowerCase();
        res.writeHead(200, Object.assign({ 'Content-Type': 'application/json' }, cors()));
        return res.end(JSON.stringify({
            mode: effMode(dev === 'stm32' ? 'stm32' : 'esp'),
            esp: state.mode,
            door: state.door,
            ts: Date.now()
        }));
    }
    if (req.method === 'GET' && u.pathname === '/api/status') {
        const now = Date.now();
        // online 保持旧含义 = ESP32 门禁主控在线（前端老逻辑兼容）
        const online = now - devSeen.esp32 < ONLINE_MS;
        const stmOnline = now - devSeen.stm32 < ONLINE_MS;
        res.writeHead(200, Object.assign({ 'Content-Type': 'application/json' }, cors()));
        return res.end(JSON.stringify({
            // mode = ESP32 上报的真实模式（前端原有逻辑不变）
            // lock = 与 mode 平行的"门锁归属"，给前端画提示用
            state: Object.assign({}, state, { lock: effMode('esp') }),
            online,
            devices: {
                esp32: { online, lastSeen: devSeen.esp32 },
                stm32: {
                    online: stmOnline,
                    lastSeen: devSeen.stm32,
                    ip: stm32Info.ip,
                    cmdCount: stm32Info.cmdCount
                }
            },
            pendingCmd: pendingCmd && now - pendingCmdAt < cmdTtl(pendingCmd) ? pendingCmd : '',
            events: events.slice(-100).reverse()
        }));
    }
    if (req.method === 'GET') return serveStatic(req, res, u.pathname);
    res.writeHead(404); res.end();
});

server.listen(PORT, () => console.log('[SmartGuard] web -> http://localhost:' + PORT));
