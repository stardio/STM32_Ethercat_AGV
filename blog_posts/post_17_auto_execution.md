# Multi-Block G-code Program Execution — The Auto Mode

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 17 of 20  
**Tags:** G-code, Automation, Sequential Execution, JavaScript, Async

---

## Beyond Single-Line Execution

Early in development, the HMI sent one G-code line at a time — the user typed each command manually in a console. This is useful for setup but impractical for production runs. A real robot needs to execute a complete program of dozens or hundreds of lines automatically.

The Auto tab adds:
- A G-code editor (multi-line `<textarea>`)
- File load/save (browser File API)
- A 3D path preview (Three.js WebGL)
- Run / Pause / Stop controls
- A program counter showing the current line

---

## The Sequential Executor

The key challenge: the robot physically takes time to execute each move. The executor must wait for each move to finish before sending the next one. If you fire all commands at once, the firmware ignores them (BUSY result) because the interpolator is already running.

The correct sequence for each G-code line:

```
1. Parse the G-code line
2. Send the motion command, wait for ACK
3. Wait for interp state == DONE
4. Send INTERP_ACK (→ transitions firmware DONE → IDLE)
5. Wait for interp state == IDLE
6. Move to next line
```

Implemented as an async JavaScript function:

```javascript
async function runProgram() {
    _programRunning = true;
    updateUI();

    for (let i = _programLine; i < _programLines.length; i++) {
        if (!_programRunning) break;   /* Stop button was pressed */

        _programLine = i;
        updateLineHighlight(i);

        const line = _programLines[i].trim();
        if (!line || line.startsWith(';')) continue;   /* skip blanks/comments */

        const cmd = parseGcode(line, _curPos);
        if (!cmd) continue;

        /* Update modal position tracking */
        _curPos = [cmd.x ?? _curPos[0],
                   cmd.y ?? _curPos[1],
                   cmd.z ?? _curPos[2]];

        /* Send command */
        const seq = sendWs(cmd);

        /* Wait for ACK */
        await waitForAck(seq, 2000);

        /* Wait for DONE */
        await waitForInterp('DONE', 30000);

        /* Acknowledge → IDLE */
        sendWs({ cmd: 'interp_ack' });

        /* Wait for IDLE */
        await waitForInterp('IDLE', 1000);
    }

    _programRunning = false;
    _programLine = 0;
    updateUI();
}
```

---

## Promise-Based Event Waiting

`waitForAck()` and `waitForInterp()` convert WebSocket message events into awaitable Promises:

```javascript
function waitForAck(seq, timeoutMs = 2000) {
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
            _ackWaiters.delete(seq);
            reject(new Error(`ACK timeout for seq ${seq}`));
        }, timeoutMs);

        _ackWaiters.set(seq, (result) => {
            clearTimeout(timer);
            if (result === 'OK') resolve();
            else reject(new Error(`NACK: ${result}`));
        });
    });
}

function waitForInterp(targetState, timeoutMs = 30000) {
    if (_interpState === targetState) return Promise.resolve();
    
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
            _interpWaiters = _interpWaiters.filter(w => w !== waiter);
            reject(new Error(`Interp timeout waiting for ${targetState}`));
        }, timeoutMs);

        const waiter = (state) => {
            if (state === targetState) {
                clearTimeout(timer);
                resolve();
                return true;   /* remove this waiter */
            }
            return false;
        };
        _interpWaiters.push(waiter);
    });
}
```

Each incoming STATUS message calls `notifyInterpWaiters()`:

```javascript
function notifyInterpWaiters(state) {
    _interpState = state;
    _interpWaiters = _interpWaiters.filter(w => !w(state));
}
```

---

## Stop and Pause

**Stop**: sets `_programRunning = false` and sends `{ cmd: 'stop' }` to the firmware. The firmware calls `Interp_Stop()` which latches all axes at their current positions.

```javascript
document.getElementById('btn-stop').onclick = () => {
    _programRunning = false;
    sendWs({ cmd: 'stop' });
};
```

**Pause**: currently not implemented — stopping and restarting from the beginning is the workflow. A future improvement would track `_programLine` and restart from the current position.

---

## 3D Path Preview

Before running the program, the 3D preview renders the complete tool path. The renderer uses Three.js and parses the G-code entirely in JavaScript:

```javascript
function buildPathGeometry(lines) {
    const points = [[0, 0, 0]];   /* start at origin */
    let cur = [0, 0, 0];

    for (const line of lines) {
        const cmd = parseGcode(line, cur);
        if (!cmd) continue;

        const end = [cmd.x, cmd.y, cmd.z];

        if (cmd.cmd === 'move_g00' || cmd.cmd === 'move_g01') {
            points.push(end);
        } else if (cmd.cmd === 'move_arc') {
            /* Approximate arc with N line segments */
            const arcPoints = sampleArc(cur, end, cmd.i, cmd.j, cmd.cw, 64);
            points.push(...arcPoints);
        }
        cur = end;
    }

    return points;
}
```

Arcs are approximated with 64 line segments for display — the actual firmware uses 5000+ segments at 1 ms resolution.

---

## Line Highlighting

As the program executes, the current line is highlighted in the editor:

```javascript
function updateLineHighlight(lineIndex) {
    const lines = _editor.value.split('\n');
    /* Scroll to current line */
    const lineHeight = parseFloat(getComputedStyle(_editor).lineHeight);
    _editor.scrollTop = lineIndex * lineHeight - _editor.clientHeight / 2;

    /* Highlight via selection (read-only during run) */
    let startPos = lines.slice(0, lineIndex).join('\n').length;
    if (lineIndex > 0) startPos++;
    const endPos = startPos + lines[lineIndex].length;
    _editor.setSelectionRange(startPos, endPos);
}
```

---

## A Complete Test Program

The circle test used to validate the arc interpolator:

```gcode
; Circle test — 83mm radius, XY plane
G00 X-981 Y-354 Z-50
G02 X-981 Y-354 I-83 J0 F100
G00 X0 Y0 Z0
```

This 3-line program:
1. Rapids to start position: ~10 seconds (at 100 mm/s)
2. Draws a 521 mm circumference circle: ~5.2 seconds
3. Rapids home: ~10 seconds

Total: ~25 seconds for a full test cycle.

---

## Next Post

The auto mode worked — but not always. The next post documents the first of three critical bugs: a subtle initialization order problem that caused G00 to run at the wrong speed when the machine was configured from flash.
