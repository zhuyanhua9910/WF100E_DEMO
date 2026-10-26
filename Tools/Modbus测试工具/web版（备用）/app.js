/**
 * app.js — SAF8100 Modbus 测试工具（Web Serial）
 *
 * 只做三件事：
 *   1. 通过 Web Serial 打开 COM 口（115200 8N1，RTU）
 *   2. 按固件约定"写 40001 起下发命令 → 读 30001 起取结果"
 *   3. 把 8 通道数据按芯片寄存器位域展开
 */

import * as P from './protocol.js';

/* ========================= 小工具 ========================= */

const $ = (id) => document.getElementById(id);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const hex2 = (v) => '0x' + (v & 0xFF).toString(16).toUpperCase().padStart(2, '0');
const hex4 = (v) => '0x' + (v & 0xFFFF).toString(16).toUpperCase().padStart(4, '0');
const pad2 = (v) => String(v).padStart(2, '0');

/* 表格里用的短标签，详情面板仍显示完整文字 */
const ERR_SHORT = { 0x00: '正常', 0x01: '登录失败', 0x02: '通信失败', 0x03: '回读不一致', 0x04: '烧写失败', 0xFE: '未测试' };

const state = {
    port: null,
    reader: null,
    writer: null,
    connected: false,
    ports: [],
    rx: new Uint8Array(0),
    slave: P.SLAVE_DEFAULT,
    baud: 115200,
    timeout: 500,
    retry: 2,
    mask: 0xFF,
    channels: [],
    selected: -1,
    taskRunning: false,
    showRaw: true,
    /* 'fw' = 本固件（PDU 40000/30000），'zero' = 标准 0 基从机 */
    addrMode: 'fw',
};

/** 写保持寄存器时 PDU 里填的起始地址 */
const writeAddr = () => (state.addrMode === 'fw' ? P.PDU_ADDR.holding : P.PDU_ADDR.zero);
/** 读输入寄存器时 PDU 里填的起始地址 */
const readAddr = () => (state.addrMode === 'fw' ? P.PDU_ADDR.input : P.PDU_ADDR.zero);

/* ========================= 日志 ========================= */

const LOG_MAX = 500;

function log(kind, msg) {
    if (kind === 'tx' || kind === 'rx') {
        if (!state.showRaw) return;
    }
    const box = $('log');
    const now = new Date();
    const ts = `${pad2(now.getHours())}:${pad2(now.getMinutes())}:${pad2(now.getSeconds())}.${String(now.getMilliseconds()).padStart(3, '0')}`;
    const label = { tx: 'TX', rx: 'RX', info: '··', ok: 'OK', warn: '!', err: 'ERR' }[kind] || '··';

    const line = document.createElement('div');
    line.className = 'log-line ' + kind;
    const t = document.createElement('span');
    t.className = 't';
    t.textContent = ts;
    const k = document.createElement('span');
    k.className = 'k';
    k.textContent = label;
    const m = document.createElement('span');
    m.className = 'm';
    m.textContent = msg;
    line.append(t, k, m);
    box.appendChild(line);

    while (box.childElementCount > LOG_MAX) {
        box.removeChild(box.firstChild);
    }
    if ($('autoScroll').checked) {
        box.scrollTop = box.scrollHeight;
    }
}

/* ========================= 串口 ========================= */

const serialSupported = () => 'serial' in navigator;

let lockChain = Promise.resolve();

/** 串行化所有事务，避免两条命令交叉 */
function withLock(fn) {
    const run = lockChain.then(fn, fn);
    lockChain = run.then(() => {}, () => {});
    return run;
}

function concatBytes(a, b) {
    const out = new Uint8Array(a.length + b.length);
    out.set(a, 0);
    out.set(b, a.length);
    return out;
}

async function readLoop() {
    try {
        for (;;) {
            const { value, done } = await state.reader.read();
            if (done) break;
            if (value && value.length) {
                state.rx = concatBytes(state.rx, value);
            }
        }
    } catch (e) {
        if (state.connected) log('err', '读取串口出错：' + e.message);
    } finally {
        if (state.connected) {
            log('warn', '串口已断开');
            await closePort(true);
        }
    }
}

async function refreshPorts() {
    if (!serialSupported()) return;
    const sel = $('portSel');
    let ports = [];
    try {
        ports = await navigator.serial.getPorts();
    } catch (e) {
        ports = [];
    }
    state.ports = ports;

    const current = sel.value;
    sel.innerHTML = '';
    const optNew = document.createElement('option');
    optNew.value = 'new';
    optNew.textContent = ports.length ? '＋ 选择新串口…' : '＋ 选择串口…';
    sel.appendChild(optNew);

    ports.forEach((p, i) => {
        const info = p.getInfo ? p.getInfo() : {};
        const tag = info.usbVendorId
            ? `VID:${info.usbVendorId.toString(16).toUpperCase()} PID:${(info.usbProductId || 0).toString(16).toUpperCase()}`
            : '串口';
        const o = document.createElement('option');
        o.value = String(i);
        o.textContent = `已授权 #${i + 1}（${tag}）`;
        sel.appendChild(o);
    });

    if (current && current !== 'new' && Number(current) < ports.length) {
        sel.value = current;
    }
}

async function connect() {
    if (!serialSupported()) {
        showBanner();
        return;
    }
    if (state.connected) return;

    let port = null;
    const v = $('portSel').value;
    try {
        if (v === 'new' || !state.ports.length) {
            port = await navigator.serial.requestPort();
        } else {
            port = state.ports[Number(v)];
        }
        if (!port) return;

        state.baud = Number($('baudSel').value) || 115200;
        await port.open({
            baudRate: state.baud,
            dataBits: 8,
            stopBits: 1,
            parity: 'none',
            flowControl: 'none',
            bufferSize: 8192,
        });
    } catch (e) {
        if (e && e.name === 'NotFoundError') {
            log('warn', '未选择串口');
        } else {
            log('err', '打开串口失败：' + (e.message || e.name));
        }
        return;
    }

    state.port = port;
    state.reader = port.readable.getReader();
    state.writer = port.writable.getWriter();
    state.rx = new Uint8Array(0);
    state.connected = true;
    port.addEventListener?.('disconnect', onPortDisconnect);
    readLoop();
    setConnUI(true);
    log('ok', `串口已打开：${state.baud} 8N1，从站地址 ${state.slave}，超时 ${state.timeout}ms`);
    await refreshPorts();
}

function onPortDisconnect() {
    if (state.connected) {
        log('warn', '检测到串口拔出');
        closePort(true);
    }
}

async function closePort(silent = false) {
    state.connected = false;
    try {
        if (state.reader) {
            await state.reader.cancel();
            state.reader.releaseLock();
        }
    } catch (e) { /* ignore */ }
    try {
        if (state.writer) state.writer.releaseLock();
    } catch (e) { /* ignore */ }
    try {
        if (state.port) await state.port.close();
    } catch (e) { /* ignore */ }

    if (state.port) state.port.removeEventListener?.('disconnect', onPortDisconnect);
    state.port = null;
    state.reader = null;
    state.writer = null;
    setConnUI(false);
    if (!silent) log('info', '串口已关闭');
}

function setConnUI(on) {
    $('dot').classList.toggle('on', on);
    $('connText').textContent = on ? `已连接 · ${state.baud}` : '未连接';
    $('btnConnect').disabled = on;
    $('btnDisconnect').disabled = !on;
}

/* ========================= 事务 ========================= */

async function waitFrame(func, timeout) {
    const t0 = performance.now();
    for (;;) {
        const got = P.extractFrame(state.rx, func);
        if (got) {
            state.rx = got.rest;
            return got.frame;
        }
        if (performance.now() - t0 >= timeout) return null;
        if (!state.connected) return null;
        await sleep(4);
    }
}

/** 单次收发（含重试），不获取锁 */
async function txn(frame, func, { timeout = state.timeout, retry = state.retry, quiet = false } = {}) {
    if (!state.connected) throw new Error('串口未连接');
    let last = '响应超时';

    for (let attempt = 0; attempt <= retry; attempt++) {
        state.rx = new Uint8Array(0);
        if (!quiet) log('tx', P.formatHex(frame));
        try {
            await state.writer.write(frame);
        } catch (e) {
            throw new Error('发送失败：' + (e.message || e.name));
        }
        const raw = await waitFrame(func, timeout);
        if (raw) {
            if (!quiet) log('rx', P.formatHex(raw));
            const r = P.parseResponse(raw, func);
            if (r.ok) return r;
            last = r.error;
            if (r.exception) break; /* 从机明确拒绝，重试无意义 */
        } else {
            last = `${timeout}ms 内无应答`;
        }
        if (attempt < retry) await sleep(35);
    }
    throw new Error(last);
}

/**
 * 一次完整"命令 + 回读"：写 40001 起的命令块（FC10），再读 30001 起的结果（FC04）。
 * 两步放在同一把锁里，避免被其它按钮插入。
 */
async function exchange({ write = null, count = 0, readFunc = 0x04, timeout = state.timeout, retry = state.retry, quiet = false }) {
    return withLock(async () => {
        if (!state.connected) throw new Error('串口未连接');
        if (write) {
            await txn(P.buildWriteMultiple(state.slave, writeAddr(), write), 0x10, { timeout, retry, quiet });
        }
        if (count > 0) {
            const r = await txn(P.buildRead(state.slave, readFunc, readAddr(), count), readFunc, { timeout, retry, quiet });
            return r.values;
        }
        return null;
    });
}

/* ========================= 业务操作 ========================= */

async function opVersion() {
    const vals = await exchange({ write: P.cmdGetVersion(), count: 16, timeout: 600 });
    const str = vals.map((v) => (v >= 32 && v < 127 ? String.fromCharCode(v) : '')).join('').trim();
    $('versionText').textContent = str || '(空)';
    log('ok', `固件版本：${str || '(空)'}`);
    return str;
}

async function opVdd(on) {
    const vals = await exchange({ write: P.cmdSetVdd(on), count: 1 });
    log(vals && vals[0] ? 'ok' : 'warn', `总电源 ${on ? '开' : '关'} → 回读 ${vals ? vals[0] : '—'}`);
}

async function opSendTask(task, mask) {
    await exchange({ write: P.cmdSetTask(task, mask), quiet: false });
    log('ok', `已下发 T${task} ${P.TASK_NAME[task]}，通道掩码 0x${mask.toString(16).toUpperCase().padStart(2, '0')}（${P.maskToText(mask)}）`);
}

async function opQueryTask(quiet = false) {
    const st = await exchange({ write: P.cmdGetTask(), count: 7, timeout: 400, retry: 0, quiet });
    return {
        busy: st[0],
        order: st[1],
        prog: st[2],
        paras: st.slice(3, 7),
    };
}

async function opReadData(start = 0, n = P.CHANNEL_COUNT) {
    const vals = await exchange({ write: P.cmdGetData(start), count: n * P.REGS_PER_CHIP, timeout: 1200, retry: 1 });
    state.channels = P.decodeChannels(vals, start);
    renderChannels();
    const t = new Date();
    $('readText').textContent = `已读取 ${state.channels.length} 通道 · ${pad2(t.getHours())}:${pad2(t.getMinutes())}:${pad2(t.getSeconds())} · ${vals.length} 个寄存器`;
    log('ok', `读取 0x610 完成：${state.channels.length} 通道 / ${vals.length} 寄存器`);
    return state.channels;
}

async function opWriteConfig(cfg) {
    const frame = P.cmdSetConfig(cfg);
    const vals = await exchange({ write: frame, count: 1 });
    log('ok', `配置已写入（10 项参数，valid=${cfg.valid ? 1 : 0} ext_en=${cfg.extEn ? 1 : 0}）→ 回读 ${vals ? vals[0] : '—'}`);
}

/* ========================= 一键测试 ========================= */

async function runTask() {
    if (state.taskRunning) return;
    if (!state.connected) {
        log('warn', '请先连接串口');
        return;
    }
    const task = Number($('taskSel').value);
    const mask = state.mask;

    if (task === P.TASK.FUSE) {
        const ok = confirm('OTP 烧写不可逆：确认要执行 T7 烧写任务吗？\n建议先用空白片验证 T5。');
        if (!ok) return;
    }

    state.taskRunning = true;
    setActionUI(true);
    const t0 = performance.now();
    setProgress(0, P.CHANNEL_COUNT, '任务已下发，等待状态…');

    try {
        await opSendTask(task, mask);
        await sleep(120);

        let done = false;
        let timeouts = 0;
        let lastProg = -1;
        const deadline = Date.now() + 120000;

        while (Date.now() < deadline) {
            let st = null;
            try {
                st = await opQueryTask(true);
            } catch (e) {
                timeouts++;
                if (timeouts % 6 === 0) {
                    log('warn', `状态查询无应答（${e.message}）—— OWI 通信期间固件会关闭 Modbus 时基，属正常现象，继续等待`);
                }
                await sleep(120);
                continue;
            }
            timeouts = 0;
            const total = P.CHANNEL_COUNT;
            setProgress(st.prog, total, st.busy ? `测试中 · 通道 ${st.prog}/${total}` : '任务收尾…');
            if (st.prog !== lastProg) {
                lastProg = st.prog;
                log('info', `进度 ${st.prog}/${total}（busy=${st.busy} order=${st.order}）`);
            }
            if (st.busy === 0 && st.order === 0) {
                done = true;
                break;
            }
            await sleep(150);
        }

        if (!done) throw new Error('等待任务结束超时（120s）');
        setProgress(P.CHANNEL_COUNT, P.CHANNEL_COUNT, '任务完成');
        log('ok', `任务完成，耗时 ${((performance.now() - t0) / 1000).toFixed(1)}s`);

        if ($('autoRead').checked) {
            await opReadData(0, P.CHANNEL_COUNT);
        }
    } catch (e) {
        log('err', '任务失败：' + e.message);
        setProgress(0, P.CHANNEL_COUNT, '失败：' + e.message);
    } finally {
        state.taskRunning = false;
        setActionUI(false);
    }
}

function setProgress(done, total, text) {
    const pct = total ? Math.max(0, Math.min(100, (done / total) * 100)) : 0;
    $('progBar').style.width = pct + '%';
    $('progText').textContent = text;
    $('progNum').textContent = `${done}/${total}`;
}

function setActionUI(busy) {
    ['btnRun', 'btnSendTask', 'btnQuery', 'btnRead', 'btnCfgWrite', 'btnVersion', 'btnVddOn', 'btnVddOff', 'btnManSend', 'btnRawSend'].forEach((id) => {
        $(id).disabled = busy;
    });
}

/* ========================= 渲染：表格 ========================= */

function cell(tr, text, cls) {    const td = document.createElement('td');
    if (cls) td.className = cls;
    if (text !== undefined && text !== null) td.textContent = text;
    tr.appendChild(td);
    return td;
}

function cell2(tr, main, sub) {
    const td = cell(tr, '');
    const v = document.createElement('div');
    v.className = 'v';
    v.textContent = main;
    td.appendChild(v);
    if (sub) {
        const s = document.createElement('div');
        s.className = 's';
        s.textContent = sub;
        td.appendChild(s);
    }
    return td;
}

function renderChannels() {
    const tbody = $('tbody');
    tbody.innerHTML = '';
    $('summary').textContent = '尚未读取';

    if (!state.channels.length) {
        const tr = document.createElement('tr');
        tr.className = 'empty-row';
        const td = document.createElement('td');
        td.colSpan = 14;
        td.textContent = '连接串口后点击「一键测试并读取」，或先读取 0x610 数据';
        tr.appendChild(td);
        tbody.appendChild(tr);
        return;
    }

    for (const c of state.channels) {
        const tr = document.createElement('tr');
        tr.dataset.ch = c.ch;
        if (c.ch === state.selected) tr.classList.add('sel');

        cell(tr, c.ch, 'ch');

        const tdStatus = cell(tr, '');
        const pill = document.createElement('span');
        pill.className = 'pill ' + c.status;
        pill.textContent = c.status === 'ok' ? '正常' : ERR_SHORT[c.err] || c.errText;
        tdStatus.appendChild(pill);

        cell2(tr, c.loginOk ? '0x55' : hex2(c.login), c.loginOk ? 'ACK' : (c.status === 'skip' ? '—' : 'NO ACK'));
        cell2(tr, hex2(c.err), c.err === 0 ? '正常' : (c.err === 0xFE ? '未测' : '异常'));
        cell2(tr, String(c.actGd), hex4(c.actGd));
        cell2(tr, String(c.actZd), hex4(c.actZd));

        const pga = P.decodePgaGain(c.pgaGain);
        cell2(tr, hex2(c.pgaGain), `${pga.g1}/${pga.g3}/${pga.g2}×`);

        const adc = P.decodeAdcCtrl(c.adcCtrl);
        cell2(tr, hex2(c.adcCtrl), `${adc.osr}/${adc.avg}`);

        const pctrl = P.decodePgaCtrl(c.pgaCtrl);
        cell2(tr, hex2(c.pgaCtrl), pctrl.vcm);

        cell2(tr, hex2(c.sysCtrl), c.sysCtrl === 0 ? '默认' : '');
        cell2(tr, hex2(c.lutSel), c.lutSel === 0 ? '默认' : '');
        cell2(tr, String(c.tempCode), c.tempCode ? '' : '—');
        cell2(tr, c.status === 'skip' ? '—' : c.temp.toFixed(2), c.status === 'skip' ? '' : '℃');
        cell2(tr, c.fuseDone ? '是' : '—', '');

        tr.addEventListener('click', () => selectChannel(c.ch));
        tbody.appendChild(tr);
    }

    const ok = state.channels.filter((c) => c.status === 'ok').length;
    const err = state.channels.filter((c) => c.status === 'err').length;
    const skip = state.channels.filter((c) => c.status === 'skip').length;
    $('summary').textContent = `${state.channels.length} 通道 · 正常 ${ok} / 异常 ${err} / 未测 ${skip}`;

    if (state.selected >= 0 && state.channels.some((x) => x.ch === state.selected)) {
        renderDetail(state.channels.find((x) => x.ch === state.selected));
    } else if (state.channels.length) {
        selectChannel(state.channels[0].ch);
    }
}

function selectChannel(ch) {
    state.selected = ch;
    Array.from($('tbody').children).forEach((tr) => {
        tr.classList.toggle('sel', Number(tr.dataset.ch) === ch);
    });
    const c = state.channels.find((x) => x.ch === ch);
    if (c) renderDetail(c);
}

/* ========================= 渲染：详情 ========================= */

function dbox(title, value, lines, bits) {
    const box = document.createElement('div');
    box.className = 'dbox';
    const h = document.createElement('h3');
    h.textContent = title;
    const span = document.createElement('span');
    span.textContent = value;
    h.appendChild(span);
    box.appendChild(h);

    (lines || []).forEach(([k, v]) => {
        const d = document.createElement('div');
        d.className = 'dline';
        const kk = document.createElement('b');
        kk.textContent = k;
        const vv = document.createElement('span');
        vv.textContent = v;
        d.append(kk, vv);
        box.appendChild(d);
    });

    if (bits && bits.length) {
        const list = document.createElement('div');
        list.className = 'bitlist';
        bits.forEach((b) => {
            const s = document.createElement('span');
            s.className = 'bit' + (b.on ? ' on' : '');
            s.textContent = b.name;
            list.appendChild(s);
        });
        box.appendChild(list);
    }
    return box;
}

function renderDetail(c) {
    $('detailTitle').textContent = `CH${c.ch} · ${c.status === 'ok' ? '正常' : c.errText}`;
    const wrap = $('detail');
    wrap.innerHTML = '';
    const grid = document.createElement('div');
    grid.className = 'detail-grid';

    grid.appendChild(dbox('基础', `CH${c.ch}`, [
        ['登录应答', c.loginOk ? '0x55 ACK' : hex2(c.login)],
        ['错误码', `${hex2(c.err)} ${c.errText}`],
        ['温度原始码', String(c.tempCode)],
        ['温度', c.status === 'skip' ? '—' : `${c.temp.toFixed(2)} ℃（×100=${c.tempX100}）`],
        ['换算校核', c.status === 'skip' ? '—' : `${P.tempFromCode(c.tempCode).toFixed(2)} ℃`],
        ['OTP 烧写', c.fuseDone ? '已完成' : '未执行'],
    ]));

    const gd = P.decodePgaGain(c.pgaGain);
    grid.appendChild(dbox('PGA_GAIN 增益', hex2(c.pgaGain), [
        ['G1（bit7-5）', `×${gd.g1}`],
        ['G3（bit4-2）', `×${gd.g3}`],
        ['G2（bit1-0）', `×${gd.g2}`],
        ['总增益', `${(gd.g1 * gd.g3 * gd.g2).toFixed(2)} ×`],
    ]));

    const ad = P.decodeAdcCtrl(c.adcCtrl);
    grid.appendChild(dbox('ADC_CTRL 采样', hex2(c.adcCtrl), [
        ['OSR（bit4-3）', String(ad.osr)],
        ['AVG_NUM（bit2-0）', `${ad.avg} 次`],
        ['SOT_EN（bit7）', ad.sot ? '1（二阶）' : '0（一阶）'],
    ]));

    const pc = P.decodePgaCtrl(c.pgaCtrl);
    grid.appendChild(dbox('PGA_CTRL 基准', hex2(c.pgaCtrl), [
        ['VREF_SEL（bit7）', pc.vref],
        ['REF_GAIN（bit6）', `×${pc.refGain}`],
        ['VCM_SEL（bit1-0）', pc.vcm],
        ['SPIN_CLKS（bit3-2）', pc.spinClk],
        ['斩波 CHOP_EN1/2', `${pc.chopEn1 ? 1 : 0} / ${pc.chopEn2 ? 1 : 0}`],
    ]));

    const sc = P.decodeSysCtrl(c.sysCtrl);
    grid.appendChild(dbox('SYS_CTRL 系统', hex2(c.sysCtrl), [['模式', sc.text]], sc.bits));

    const ls = P.decodeLutSel(c.lutSel);
    grid.appendChild(dbox('LUT_SEL 校准', hex2(c.lutSel), [['LOAD_LUT', String(ls.loadLut)]], ls.bits));

    grid.appendChild(dbox('DAC 当前值', '', [
        ['增益 DAC', `${c.actGd}（${hex4(c.actGd)}）`],
        ['零点 DAC', `${c.actZd}（${hex4(c.actZd)}）`],
        ['14bit 上限', '0x3FFF = 16383'],
    ]));

    wrap.appendChild(grid);
}

/* ========================= 掩码 / 通道按钮 ========================= */

function buildChips() {
    const box = $('chips');
    box.innerHTML = '';
    for (let i = 0; i < P.CHANNEL_COUNT; i++) {
        const b = document.createElement('div');
        b.className = 'chip';
        b.textContent = String(i);
        b.dataset.ch = String(i);
        b.addEventListener('click', () => {
            state.mask ^= 1 << i;
            syncMask();
        });
        box.appendChild(b);
    }
}

function syncMask() {
    Array.from($('chips').children).forEach((c) => {
        const i = Number(c.dataset.ch);
        c.classList.toggle('on', !!(state.mask & (1 << i)));
    });
    $('maskVal').value = '0x' + state.mask.toString(16).toUpperCase().padStart(2, '0');
    $('maskText').textContent = P.maskToText(state.mask);
}

function setMask(v) {
    state.mask = v & 0xFF;
    syncMask();
}

/* ========================= 配置面板 ========================= */

function fillSelect(sel, items) {
    sel.innerHTML = '';
    items.forEach(([value, label]) => {
        const o = document.createElement('option');
        o.value = String(value);
        o.textContent = label;
        sel.appendChild(o);
    });
}

function buildConfigOptions() {
    fillSelect($('cfgG1'), P.PGA_G1.map((g, i) => [i, `G1 ×${g}`]));
    fillSelect($('cfgG3'), P.PGA_G3.map((g, i) => [i, `G3 ×${g}`]));
    fillSelect($('cfgG2'), P.PGA_G2.map((g, i) => [i, `G2 ×${g}`]));
    fillSelect($('cfgVref'), [[0, '内部 2.5V'], [1, 'VDD/2.0']]);
    fillSelect($('cfgRefGain'), [[0, '×1'], [1, '×1.6384']]);
    fillSelect($('cfgVcm'), P.VCM_LIST.map((v, i) => [i, `VCM ${v}`]));
    fillSelect($('cfgOsr'), P.OSR_LIST.map((v, i) => [i, `OSR ${v}`]));
    fillSelect($('cfgAvg'), P.AVG_LIST.map((v, i) => [i, `平均 ${v} 次`]));
}

function cfgFromUI() {
    return {
        actGd: Number($('cfgGd').value) || 0,
        actZd: Number($('cfgZd').value) || 0,
        pgaGain: P.makePgaGain(Number($('cfgG1').value), Number($('cfgG3').value), Number($('cfgG2').value)),
        pgaCtrl: P.makePgaCtrl(Number($('cfgVref').value), Number($('cfgRefGain').value), Number($('cfgVcm').value)),
        adcCtrl: P.makeAdcCtrl(Number($('cfgOsr').value), Number($('cfgAvg').value)),
        sysCtrl: parseInt(($('cfgSys').value || '0').replace(/^0x/i, ''), 16) & 0xFF || 0,
        lutSel: parseInt(($('cfgLut').value || '0').replace(/^0x/i, ''), 16) & 0xFF || 0,
        valid: $('cfgValid').checked,
        extEn: $('cfgExt').checked,
    };
}

function applyCfgToUI(cfg, opts = {}) {
    $('cfgGd').value = String(cfg.actGd);
    $('cfgZd').value = String(cfg.actZd);
    const g = P.decodePgaGain(cfg.pgaGain);
    $('cfgG1').value = String(P.PGA_G1.indexOf(g.g1));
    $('cfgG3').value = String(P.PGA_G3.indexOf(g.g3));
    $('cfgG2').value = String(P.PGA_G2.indexOf(g.g2));
    const pc = P.decodePgaCtrl(cfg.pgaCtrl);
    $('cfgVref').value = String(pc.vref === 'VDD/2.0' ? 1 : 0);
    $('cfgRefGain').value = String(pc.refGain > 1 ? 1 : 0);
    $('cfgVcm').value = String(P.VCM_LIST.indexOf(pc.vcm));
    const ad = P.decodeAdcCtrl(cfg.adcCtrl);
    $('cfgOsr').value = String(P.OSR_LIST.indexOf(ad.osr));
    $('cfgAvg').value = String(P.AVG_LIST.indexOf(ad.avg));
    $('cfgSys').value = pad2(cfg.sysCtrl.toString(16).toUpperCase());
    $('cfgLut').value = pad2(cfg.lutSel.toString(16).toUpperCase());
    $('cfgValid').checked = !!cfg.valid;
    $('cfgExt').checked = !!cfg.extEn;
    if (opts.save !== false) saveSettings();
    updateCfgPreview();
}

function updateCfgPreview() {
    const cfg = cfgFromUI();
    const g = P.decodePgaGain(cfg.pgaGain);
    const ad = P.decodeAdcCtrl(cfg.adcCtrl);
    const pc = P.decodePgaCtrl(cfg.pgaCtrl);
    const frame = P.buildWriteMultiple(state.slave, writeAddr(), P.cmdSetConfig(cfg));

    $('sysText').textContent = P.decodeSysCtrl(cfg.sysCtrl).text;
    $('lutText').textContent = P.decodeLutSel(cfg.lutSel).text;

    $('cfgPreview').textContent =
        `0x45=${hex2(cfg.pgaGain)} (${g.g1}/${g.g3}/${g.g2}×)   0x48=${hex2(cfg.pgaCtrl)} (${pc.vcm} / ${pc.vref} ×${pc.refGain})\n` +
        `0x4B=${hex2(cfg.adcCtrl)} (OSR${ad.osr} / AVG${ad.avg})   0x44=${hex2(cfg.sysCtrl)}   0x47=${hex2(cfg.lutSel)}\n` +
        `帧：${P.formatHex(frame)}`;
}

/* ========================= 设置持久化 ========================= */

const LS_KEY = 'saf8100.modbus.tool.v1';

function saveSettings() {
    try {
        localStorage.setItem(LS_KEY, JSON.stringify({
            baud: $('baudSel').value,
            mask: state.mask,
            task: $('taskSel').value,
            addrMode: state.addrMode,
            cfg: cfgFromUI(),
        }));
    } catch (e) { /* ignore */ }
}

function loadSettings() {
    try {
        const raw = localStorage.getItem(LS_KEY);
        if (!raw) return false;
        const s = JSON.parse(raw);
        if (s.baud) $('baudSel').value = s.baud;
        if (typeof s.mask === 'number') setMask(s.mask);
        if (s.task) $('taskSel').value = s.task;
        if (s.addrMode) {
            $('addrMode').value = s.addrMode;
            state.addrMode = s.addrMode;
        }
        if (s.cfg) applyCfgToUI(s.cfg, { save: false });
        return true;
    } catch (e) {
        return false;
    }
}

/* ========================= CSV ========================= */

function exportCsv() {
    if (!state.channels.length) {
        log('warn', '还没有数据可以导出');
        return;
    }
    const head = ['通道', '状态', '登录', '错误码', '错误说明', '增益DAC', '零点DAC', 'PGA_GAIN', 'ADC_CTRL', 'PGA_CTRL', 'SYS_CTRL', 'LUT_SEL', '温度码', '温度(℃)', '烧写'];
    const lines = [head.join(',')];
    for (const c of state.channels) {
        lines.push([
            c.ch,
            c.status === 'ok' ? '正常' : c.status === 'skip' ? '未测试' : '异常',
            hex2(c.login),
            hex2(c.err),
            c.errText,
            c.actGd,
            c.actZd,
            hex2(c.pgaGain),
            hex2(c.adcCtrl),
            hex2(c.pgaCtrl),
            hex2(c.sysCtrl),
            hex2(c.lutSel),
            c.tempCode,
            c.status === 'skip' ? '' : c.temp.toFixed(2),
            c.fuseDone ? '是' : '',
        ].join(','));
    }
    const blob = new Blob(['\uFEFF' + lines.join('\r\n')], { type: 'text/csv;charset=utf-8' });
    const a = document.createElement('a');
    const now = new Date();
    a.href = URL.createObjectURL(blob);
    a.download = `saf8100_${now.getFullYear()}${pad2(now.getMonth() + 1)}${pad2(now.getDate())}_${pad2(now.getHours())}${pad2(now.getMinutes())}${pad2(now.getSeconds())}.csv`;
    a.click();
    URL.revokeObjectURL(a.href);
    log('info', '已导出 CSV');
}

/* ========================= 手动指令 ========================= */

function parseU16List(text) {
    return text.split(/[,\s]+/).filter(Boolean).map((t) => {
        const s = t.trim();
        const v = /^0x/i.test(s) ? parseInt(s, 16) : Number(s);
        if (!Number.isFinite(v)) throw new Error(`无法解析数值：${s}`);
        return v & 0xFFFF;
    });
}

function parseHexBytes(text) {
    const clean = text.replace(/0x/gi, '').replace(/[^0-9a-fA-F]/g, '');
    if (clean.length < 2) throw new Error('十六进制长度不足');
    const bytes = [];
    for (let i = 0; i + 1 < clean.length; i += 2) {
        bytes.push(parseInt(clean.slice(i, i + 2), 16));
    }
    return Uint8Array.from(bytes);
}

async function manualSend() {
    const fc = Number($('manFunc').value);
    const addr = Number($('manAddr').value) || 0;
    try {
        if (fc === 3 || fc === 4) {
            const count = Number($('manCount').value) || 1;
            const frame = P.buildRead(state.slave, fc, addr, count);
            const r = await withLock(() => txn(frame, fc));
            log('ok', `FC${fc} 读 ${count} 个寄存器：${r.values.map((v) => hex4(v)).join(' ')}`);
        } else {
            const values = parseU16List($('manValues').value || '');
            if (!values.length) throw new Error('写入数据为空');
            const frame = P.buildWriteMultiple(state.slave, addr, values);
            const r = await withLock(() => txn(frame, 0x10));
            log('ok', `FC10 ${r.note}`);
        }
    } catch (e) {
        log('err', '手动指令失败：' + e.message);
    }
}

async function rawSend() {
    try {
        let bytes = parseHexBytes($('manRaw').value || '');
        if (bytes.length >= 2) {
            /* 若最后两字节不是正确 CRC，则自动补 */
            const body = bytes.slice(0, -2);
            const tail = bytes[bytes.length - 2] | (bytes[bytes.length - 1] << 8);
            if (P.crc16(body) !== tail) {
                bytes = P.buildFrame(bytes[0], Array.from(bytes.slice(1)));
                log('info', '已自动重算 CRC');
            }
        }
        await withLock(async () => {
            state.rx = new Uint8Array(0);
            log('tx', P.formatHex(bytes));
            await state.writer.write(bytes);
            await sleep(600);
            if (state.rx.length) log('rx', P.formatHex(state.rx));
            else log('warn', '600ms 内无任何应答');
        });
    } catch (e) {
        log('err', '原始帧发送失败：' + e.message);
    }
}

/* ========================= 演示数据 ========================= */

function demoFill() {
    const regs = [];
    for (let ch = 0; ch < P.CHANNEL_COUNT; ch++) {
        const skip = ch === 7;
        const err = ch === 5 ? 0x02 : 0;
        const code = skip ? 0 : 36080 + ch * 9;
        const x100 = skip ? 0 : Math.round(P.tempFromCode(code) * 100);
        regs.push(
            ((err === 0 && !skip ? 0x55 : 0x00) << 8) | (skip ? 0xFE : err),
            skip ? 0 : 10000 + ch * 3,
            skip ? 0 : 16383,
            (0x52 << 8) | 0x15,
            (0x43 << 8) | 0x00,
            (0x00 << 8) | 0x00,
            code,
            x100 & 0xFFFF,
        );
    }
    state.channels = P.decodeChannels(regs, 0);
    state.selected = -1;
    renderChannels();
    $('summary').textContent += ' · 演示数据';
    $('readText').textContent = '演示数据（非真实测量）';
    log('warn', '已填充演示数据，仅用于预览界面，未与任何硬件通信');
}

/* ========================= 事件绑定 ========================= */

function bind() {
    $('btnConnect').addEventListener('click', connect);
    $('btnDisconnect').addEventListener('click', () => closePort(false));
    $('portSel').addEventListener('change', () => { /* 选择后点连接 */ });

    $('btnVersion').addEventListener('click', wrap('读版本', opVersion));
    $('btnVddOn').addEventListener('click', wrap('总电源开', () => opVdd(true)));
    $('btnVddOff').addEventListener('click', wrap('总电源关', () => opVdd(false)));

    $('btnRun').addEventListener('click', runTask);
    $('btnSendTask').addEventListener('click', wrap('下发任务', () => opSendTask(Number($('taskSel').value), state.mask)));
    $('btnQuery').addEventListener('click', wrap('查询状态', async () => {
        const st = await opQueryTask(false);
        log('ok', `busy=${st.busy} order=${st.order} prog=${st.prog}/${P.CHANNEL_COUNT} paras=${st.paras.join(',')}`);
        setProgress(st.prog, P.CHANNEL_COUNT, st.busy ? '任务进行中' : '空闲');
    }));

    $('btnRead').addEventListener('click', wrap('读取数据', () => opReadData(0, P.CHANNEL_COUNT)));
    $('btnCsv').addEventListener('click', exportCsv);

    $('btnCfgWrite').addEventListener('click', wrap('下发配置', () => opWriteConfig(cfgFromUI())));
    $('btnCfgReset').addEventListener('click', () => {
        applyCfgToUI({ actGd: 10000, actZd: 16383, pgaGain: 0x52, pgaCtrl: 0x43, adcCtrl: 0x15, sysCtrl: 0, lutSel: 0, valid: true, extEn: false });
        log('info', '已恢复官方 demo 默认配置（需点「下发配置」才会写入芯片）');
    });

    $('btnMaskAll').addEventListener('click', () => setMask(0xFF));
    $('btnMaskNone').addEventListener('click', () => setMask(0x00));

    $('maskVal').addEventListener('change', () => {
        const raw = $('maskVal').value.replace(/^0x/i, '').trim();
        const v = parseInt(raw, 16);
        setMask(Number.isFinite(v) ? v : 0xFF);
        saveSettings();
    });

    ['cfgGd', 'cfgZd', 'cfgG1', 'cfgG3', 'cfgG2', 'cfgVref', 'cfgRefGain', 'cfgVcm', 'cfgOsr', 'cfgAvg', 'cfgSys', 'cfgLut', 'cfgValid', 'cfgExt'].forEach((id) => {
        $(id).addEventListener('change', () => { updateCfgPreview(); saveSettings(); });
        $(id).addEventListener('input', updateCfgPreview);
    });

    $('taskSel').addEventListener('change', saveSettings);
    $('baudSel').addEventListener('change', saveSettings);

    $('btnManSend').addEventListener('click', manualSend);
    $('btnRawSend').addEventListener('click', rawSend);

    $('addrMode').addEventListener('change', () => {
        state.addrMode = $('addrMode').value;
        saveSettings();
        updateCfgPreview();
        log('info', state.addrMode === 'fw'
            ? '地址基准：40000 / 30000（匹配本固件）'
            : '地址基准：0 / 0（标准 0 基从机）');
    });

    $('manFunc').addEventListener('change', () => {
        const isWrite = $('manFunc').value === '16';
        const cur = $('manAddr').value.trim();
        if (cur === '' || cur === '0' || cur === '30000' || cur === '40000') {
            $('manAddr').value = isWrite ? (state.addrMode === 'fw' ? '40000' : '0') : (state.addrMode === 'fw' ? '30000' : '0');
        }
    });

    $('showRaw').addEventListener('change', () => { state.showRaw = $('showRaw').checked; });
    $('btnClearLog').addEventListener('click', () => { $('log').innerHTML = ''; });
    $('btnDemo').addEventListener('click', demoFill);

    document.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' && (e.target.id === 'manRaw' || e.target.id === 'manValues')) {
            e.target.id === 'manRaw' ? rawSend() : manualSend();
        }
    });

    if (serialSupported()) {
        navigator.serial.addEventListener('disconnect', onPortDisconnect);
    }
}

/** 统一捕获异常并写日志 */
function wrap(name, fn) {
    return async () => {
        if (!state.connected) {
            log('warn', `请先连接串口（${name}）`);
            return;
        }
        try {
            await fn();
        } catch (e) {
            log('err', `${name}失败：${e.message}`);
        }
    };
}

function showBanner() {
    const b = $('banner');
    const fileMode = location.protocol === 'file:';
    b.innerHTML = fileMode
        ? '当前是以 <b>file://</b> 方式打开的，浏览器不允许访问串口。请关闭本页，双击同目录下的 <b>启动工具.bat</b>，用 Edge / Chrome 从 http://127.0.0.1 打开。'
        : '当前环境不支持 <b>Web Serial</b>（预览面板内置浏览器不含串口能力）。请双击同目录下的 <b>启动工具.bat</b>，用 Edge 或 Chrome 打开。';
    b.classList.remove('hidden');
}

/* ========================= 初始化 ========================= */

function init() {
    buildChips();
    buildConfigOptions();
    setMask(0xFF);
    applyCfgToUI({ actGd: 10000, actZd: 16383, pgaGain: 0x52, pgaCtrl: 0x43, adcCtrl: 0x15, sysCtrl: 0, lutSel: 0, valid: true, extEn: false }, { save: false });
    loadSettings();
    state.addrMode = $('addrMode').value;
    state.showRaw = $('showRaw').checked;
    bind();

    if (!serialSupported()) {
        showBanner();
        $('btnConnect').disabled = true;
    } else {
        refreshPorts();
    }

    log('info', '工具已就绪：115200 8N1 · 从站 1 · 命令写 PDU 40000（=40001）/ 结果读 PDU 30000（=30001）');

    if (new URLSearchParams(location.search).get('demo') === '1') {
        demoFill();
    }
}

init();
