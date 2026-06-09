# Web HMI — Real-Time Dashboard in Plain HTML/JS

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 11 of 20  
**Tags:** Web HMI, JavaScript, WebSocket, Dashboard, UX

---

## Why No Framework?

The Web HMI is a single `index.html` file — no React, no Vue, no bundler, no node_modules. Just HTML, CSS, and vanilla JavaScript.

Reasons:
- **Zero build step** — open the file in any browser, it works
- **Easy to modify** — anyone familiar with web basics can read and change it
- **No version rot** — no `npm audit` warnings six months later
- **Serves directly** from the bridge's HTTP server

The only external dependency is the WebSocket connection to `bridge.py`.

---

## Layout Structure

```
┌────────────────────────────────────────────────────────┐
│  STATUS BAR — connection, interp state, all-axes ready │
├────────────┬───────────────────────────────────────────┤
│  DRO       │  JOG PANEL                                │
│  X:  -981  │  Step: 0.001 / 0.01 / 0.1 / 1 / 10 mm   │
│  Y:  -354  │  Speed: ×0.01 / ×0.1 / ×1 / ×10          │
│  Z:   -50  │  X+  X-  Y+  Y-  Z+  Z-  (hold buttons)  │
├────────────┴───────────────────────────────────────────┤
│  TABS: [Manual] [Parameters] [Auto] [Diagnostics]     │
├────────────────────────────────────────────────────────┤
│  TAB CONTENT (switches based on active tab)           │
└────────────────────────────────────────────────────────┘
```

The DRO (Digital Read-Out) is always visible regardless of which tab is active.

---

## Real-Time DRO

The DRO updates every time a STATUS packet arrives (every 10 ms):

```javascript
ws.onmessage = (event) => {
    const msg = JSON.parse(event.data);
    if (msg.type === 'status') {
        updateDRO(msg.axes);
        updateStatusBar(msg);
        checkInterpDone(msg);
    }
};

function updateDRO(axes) {
    const names = ['X', 'Y', 'Z'];
    names.forEach((n, i) => {
        const pos = axes[i].pos_mm;
        document.getElementById(`dro-${n}`).textContent =
            pos.toFixed(3);  // 3 decimal places = 0.001 mm = 1 µm
    });
}
```

The DRO cells have a fixed height and monospace font so the numbers don't cause layout shifts as they update 100 times per second.

---

## JOG Controls

Continuous JOG buttons use `pointerdown` / `pointerup` events (works with both mouse and touch):

```javascript
function setupJogButton(elemId, axis, direction) {
    const btn = document.getElementById(elemId);
    
    btn.addEventListener('pointerdown', () => {
        _jogAxis  = axis;
        _jogDir   = direction;
        _jogVel   = computeJogVel(axis);
        startJog();
    });
    
    btn.addEventListener('pointerup',    stopJog);
    btn.addEventListener('pointerleave', stopJog);
    btn.addEventListener('pointercancel',stopJog);
}

function startJog() {
    sendJogVelocity();   // immediately
    _jogTimer = setInterval(sendJogVelocity, 50);   // repeat every 50 ms
}

function stopJog() {
    clearInterval(_jogTimer);
    send({ cmd: 'jog_stop' });
    send({ cmd: 'set_param', axis: _jogAxis,
           param_id: PARAM.RAMP_VEL, value: 0 });
}
```

`pointerleave` is important — if the user's finger slides off the button while pressing, JOG still stops. Without this, the axis keeps running and the user loses control.

---

## Parameter Editor

The Parameters tab shows a table with one row per axis and one column per parameter. Values are fetched on page load via `param_read_req`:

```javascript
function loadParams() {
    send({ cmd: 'param_read_req' });
}

// Handle incoming param_report
if (msg.type === 'param_report') {
    pmLoaded = msg.axes;    // cache for JOG velocity calculation
    renderParamTable(msg.axes);
}

function renderParamTable(axes) {
    const tbody = document.getElementById('param-tbody');
    tbody.innerHTML = '';
    const params = ['unit_scale','profile_velocity','profile_accel_ms',
                    'profile_decel_ms','torque_limit','limit_plus_user',
                    'limit_minus_user'];
    
    params.forEach(pname => {
        const row = tbody.insertRow();
        row.insertCell().textContent = pname;
        axes.forEach((axp, i) => {
            const td = row.insertCell();
            const input = document.createElement('input');
            input.type = 'number';
            input.value = axp[pname];
            input.dataset.axis   = i;
            input.dataset.param  = PARAM_IDS[pname];
            input.addEventListener('change', onParamChange);
            td.appendChild(input);
        });
    });
}
```

Each input's `change` event sends a `set_param` command immediately. There is no "Apply" button — parameters take effect on the drive in real time (subject to the SDO stability gate).

---

## Auto Tab — G-code Editor and 3D Path Monitor

The Auto tab has three sections:
1. **G-code editor** — a `<textarea>` for writing multi-line G-code programs
2. **File management** — load from a `<input type="file">`, save with the download API
3. **3D path preview** — a WebGL canvas (using Three.js) that visualizes the tool path

The 3D preview parses the G-code locally in JavaScript and draws line segments and arcs. It provides a sanity check before sending the program to the robot. The actual robot position (from STATUS packets) is shown as a colored dot moving along the path during execution.

---

## G-code Parser in JavaScript

```javascript
function parseGcode(raw) {
    const line = raw.replace(/[;(].*/,'').trim().toUpperCase();
    const words = {};
    const re = /([A-Z])([-+]?[\d.]+)/g;
    let m;
    while ((m = re.exec(line)) !== null)
        words[m[1]] = parseFloat(m[2]);

    const g = words['G'];
    const x = words['X'] ?? curPos[0];
    const y = words['Y'] ?? curPos[1];
    const z = words['Z'] ?? curPos[2];
    const f = words['F'] ?? 50.0;

    if (g === 0)  return { cmd:'move_g00', x, y, z };
    if (g === 1)  return { cmd:'move_g01', x, y, z, feed: f };
    if (g === 2)  return { cmd:'move_arc', x, y, z,
                           i: words['I']??0, j: words['J']??0, k: words['K']??0,
                           feed: f, plane: words['P']??0, cw: true };
    if (g === 3)  return { cmd:'move_arc', x, y, z,
                           i: words['I']??0, j: words['J']??0, k: words['K']??0,
                           feed: f, plane: words['P']??0, cw: false };
    return null;
}
```

The `??` (nullish coalescing) operator elegantly handles modal values: if X is not in this line, use the last known X position.

---

## Status Indicators

Color-coded status chips update in real time:

- **IDLE** (green) — interpolator idle, ready for commands
- **MOVING** (yellow/amber) — interpolator executing a move
- **DONE** (blue) — move complete, waiting for ACK
- **FAULT** (red) — drive fault on any axis
- **DISCONNECTED** (gray) — WebSocket not connected

The status bar also shows: individual axis CiA402 states, torque bars (CSS gradient bars driven by `actual_torque`), and a connection indicator.

---

## Next Post

With the HMI working for manual operation, the next post adds programmatic motion: **the G00 rapid move** — first step of the G-code interpreter, and how it integrates with FreeRTOS task communication.
