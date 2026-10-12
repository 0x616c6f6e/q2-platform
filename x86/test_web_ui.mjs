/** Exercise the real embedded page in an isolated headless Chrome profile (Node 22+). */
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdtemp, readFile, writeFile, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {resolve, join} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';

const [runner, appDir, chromePath, screenshot] = process.argv.slice(2).map(p => resolve(p));
assert(runner && appDir && chromePath && screenshot,
    'Usage: node test_web_ui.mjs RUNNER KEY_DEMO_RUN_DIR CHROME SCREENSHOT.png');
const directory = await mkdtemp(join(tmpdir(), 'q2-key-ui-'));
let runtime, browser, socket, logs = '', sequence = 0;
const pending = new Map();
async function until(test, message, timeout = 6000) {
    const end = Date.now() + timeout;
    while (Date.now() < end) {
        if (await test()) return;
        await delay(50);
    }
    throw Error(message + '\n' + logs);
}
function call(method, params = {}) {
    const id = ++sequence;
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => { pending.delete(id); reject(Error('CDP timeout: ' + method)); }, 5000);
        pending.set(id, {resolve, reject, timer});
        socket.send(JSON.stringify({id, method, params}));
    });
}
async function evaluate(expression) {
    const result = await call('Runtime.evaluate', {expression, returnByValue:true, awaitPromise:true});
    assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
    return result.result.value;
}
async function stop(process) {
    if (!process || process.exitCode !== null) return;
    const exited = new Promise(resolve => process.once('exit', resolve));
    process.kill();
    await Promise.race([exited, delay(3000)]);
}
try {
    runtime = spawn(runner, [join(appDir, 'manifest.json'), join(appDir, 'main.lua'),
        '--headless', '--frames', '6000', '--web-port', '0', '--grant', 'display.ui',
        '--grant', 'input.keys', '--grant', 'system.log'], {windowsHide:true});
    runtime.stdout.on('data', data => { logs += data; });
    runtime.stderr.on('data', data => { logs += data; });
    await until(() => /http:\/\/127\.0\.0\.1:\d+\//.test(logs), 'Runner did not start');
    const url = logs.match(/http:\/\/127\.0\.0\.1:\d+\//)[0];
    browser = spawn(chromePath, ['--headless', '--disable-gpu', '--no-first-run',
        '--no-default-browser-check', '--hide-scrollbars', '--window-size=1200,960',
        '--remote-debugging-port=0', '--user-data-dir=' + directory, url],
        {windowsHide:true, stdio:'ignore'});
    let port;
    await until(async () => {
        try { port = (await readFile(join(directory, 'DevToolsActivePort'), 'utf8')).split('\n')[0]; return true; }
        catch { return false; }
    }, 'Chrome did not start');
    let page;
    await until(async () => {
        const targets = await (await fetch('http://127.0.0.1:' + port + '/json/list')).json();
        page = targets.find(target => target.type === 'page' && target.url === url);
        return !!page;
    }, 'Page not found');
    socket = new WebSocket(page.webSocketDebuggerUrl);
    await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
    socket.onmessage = event => {
        const value = JSON.parse(event.data), job = pending.get(value.id);
        if (!job) return;
        clearTimeout(job.timer); pending.delete(value.id);
        if (value.error) job.reject(Error(JSON.stringify(value.error))); else job.resolve(value.result);
    };
    await until(() => evaluate("document.querySelectorAll('.key:not(:disabled)').length === 5"),
        'Registered keys were not rendered');
    const state = async () => (await fetch(url + 'api/state')).json();
    const initial = await state(), gap = initial.timing.double_gap_ms, long = initial.timing.long_press_ms;
    async function reports(count) {
        await until(async () => (await state()).reports >= count, 'Missing key report');
        assert.equal((await state()).reports, count);
    }
    async function pointer(name, type) {
        const point = await evaluate(`(() => {
            const r = document.querySelector('[aria-label="${name}"]').getBoundingClientRect();
            return {x:r.x+r.width/2,y:r.y+r.height/2};
        })()`);
        await call('Input.dispatchMouseEvent', {type, ...point, button:'left',
            buttons:type === 'mousePressed' ? 1 : 0, clickCount:1});
    }
    async function click(name) {
        await pointer(name, 'mousePressed'); await delay(80); await pointer(name, 'mouseReleased');
    }
    await click('PREV'); await reports(1);
    await click('NEXT'); await delay(50); await click('NEXT'); await reports(2);
    await pointer('PLAY', 'mousePressed'); await delay(long + 100); await reports(3);
    assert.equal((await state()).keys.find(k => k.name === 'PLAY').pressed, true);
    await pointer('PLAY', 'mouseReleased');
    // Focused keyboard repeats must not become extra presses.
    await evaluate("document.querySelector('[aria-label=BACK]').focus()");
    await call('Input.dispatchKeyEvent', {type:'keyDown', key:' ', code:'Space', windowsVirtualKeyCode:32});
    await delay(80);
    await call('Input.dispatchKeyEvent', {type:'keyDown', key:' ', code:'Space', autoRepeat:true});
    await call('Input.dispatchKeyEvent', {type:'keyUp', key:' ', code:'Space', windowsVirtualKeyCode:32});
    await reports(4);
    await pointer('POWER', 'mousePressed');
    await evaluate("window.dispatchEvent(new Event('blur'))");
    await pointer('POWER', 'mouseReleased'); await delay(gap + 100);
    const final = await state();
    assert.equal(final.reports, 4);
    assert(final.keys.every(key => !key.pressed));
    assert.deepEqual(final.events.map(event => [event.name, event.action]),
        [['PREV',1], ['NEXT',2], ['PLAY',3], ['BACK',1]]);
    for (const text of ['KEY PREV=SINGLE', 'KEY NEXT=DOUBLE', 'KEY PLAY=LONG', 'KEY BACK=SINGLE'])
        assert(logs.includes(text), text + ' missing from real Lua log');
    await until(() => evaluate("document.getElementById('reports').textContent === '4 events'"), 'Stale UI');
    const errors = await evaluate("document.getElementById('error').textContent");
    assert.equal(errors, '');
    // Operate the actual device forms, including draft retention and failure modes.
    await until(() => evaluate("!document.querySelector('#battery-form button').disabled"), 'Data form disabled');
    await evaluate(`document.getElementById('battery-percent').value='42';
        document.getElementById('battery-voltage_mv').value='3800';
        document.getElementById('battery-charging').checked=true;`);
    await delay(500);
    assert.equal(await evaluate("document.getElementById('battery-percent').value"), '42');
    await evaluate("document.querySelector('#battery-form button[type=submit]').click()");
    await until(async () => (await state()).simulation.battery.percent === 42, 'Battery form failed');
    assert.equal((await state()).simulation.battery.charging, true);
    for (const group of ['wifi', 'ble']) {
        await evaluate(`document.querySelector('#${group}-form button[type=submit]').click()`);
        await until(async () => (await state()).simulation[group].mode === 'value', group + ' form failed');
    }
    await evaluate("document.getElementById('wifi-mode').value='error'; document.getElementById('wifi-mode').dispatchEvent(new Event('change'))");
    assert(await evaluate("document.getElementById('wifi-rssi_dbm').disabled"));
    await evaluate("document.querySelector('#wifi-form button[type=submit]').click()");
    await until(async () => (await state()).simulation.wifi.mode === 'error', 'Error mode failed');
    await evaluate("document.querySelector('#wifi-form [data-reset]').click()");
    await until(async () => (await state()).simulation.wifi.mode === 'off', 'Reset failed');
    await evaluate("document.getElementById('wifi-mode').value='value'; document.getElementById('wifi-mode').dispatchEvent(new Event('change')); document.querySelector('#wifi-form button[type=submit]').click()");
    await until(async () => (await state()).simulation.wifi.mode === 'value', 'Wi-Fi restore failed');
    await until(() => evaluate("document.getElementById('battery-applied').textContent.includes('42%')"), 'Stale battery label');
    await call('Emulation.setDeviceMetricsOverride', {width:1200,height:960,deviceScaleFactor:1,mobile:false});
    await delay(100);
    const image = await call('Page.captureScreenshot', {format:'png', captureBeyondViewport:true});
    await writeFile(screenshot, Buffer.from(image.data, 'base64'));
    await call('Emulation.setDeviceMetricsOverride', {width:375,height:900,deviceScaleFactor:1,mobile:true});
    assert(await evaluate('document.documentElement.scrollWidth <= window.innerWidth'), 'Mobile overflow');
    const rectangles = await evaluate(`Object.fromEntries([...document.querySelectorAll('.key')].map(b => {
        const r=b.getBoundingClientRect(); return [b.getAttribute('aria-label'), {x:r.x,y:r.y,w:r.width,h:r.height}];
    }))`);
    assert(rectangles.POWER.y < rectangles.PLAY.y && rectangles.BACK.y > rectangles.PLAY.y);
    assert(rectangles.PREV.x < rectangles.PLAY.x && rectangles.NEXT.x > rectangles.PLAY.x);
    console.log('PASS: device forms, draft retention, apply/reset/error modes; Chrome circular layout, real pointer single/double/hold, keyboard repeat, blur cancel, Lua delivery, mobile width');
} finally {
    if (socket) socket.close();
    for (const job of pending.values()) clearTimeout(job.timer);
    await stop(browser); await stop(runtime);
    // Only this test-created isolated profile is removed.
    await rm(directory, {recursive:true, force:true, maxRetries:4, retryDelay:200});
}
