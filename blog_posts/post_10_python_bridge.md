# Python WebSocket Bridge — Serial Port to Browser

**Series:** STM32 EtherCAT Cartesian Robot Controller  
**Post:** 10 of 20  
**Tags:** Python, WebSocket, asyncio, Serial, Bridge

---

## The Role of the Bridge

The browser cannot open a serial port directly (Web Serial API is available but limited). The Python bridge solves this:

```
Browser  ←──── WebSocket JSON ────→  bridge.py  ←──── UART SLIP ────→  STM32
```

The bridge is a translator, not a gateway. It speaks two completely different languages:
- **Browser side**: WebSocket, JSON, human-readable command names
- **STM32 side**: UART, SLIP binary frames, packed C structs

The bridge keeps no motion state — it just translates and relays. All timing decisions and interpolation state live in the firmware.

---

## Architecture: asyncio Single-Threaded

The bridge uses Python's `asyncio` event loop with three coroutines:

```
asyncio event loop
├── websocket_handler(ws)   — one coroutine per connected client
├── serial_reader()         — reads UART, decodes SLIP, broadcasts to all clients
└── serial_reconnect()      — reconnects if the COM port disappears
```

No threads, no locks. The asyncio scheduler cooperates between coroutines at every `await` point. This is appropriate because the bottleneck is I/O (serial at 921600 baud, WebSocket), not computation.

---

## SLIP Decoder

```python
class SlipDecoder:
    def __init__(self):
        self._buf  = bytearray()
        self._esc  = False
        self._in_frame = False

    def feed(self, byte: int) -> Optional[bytes]:
        if byte == SLIP_END:
            if self._in_frame and len(self._buf) >= 4:
                frame = bytes(self._buf)
                self._buf.clear()
                self._in_frame = False
                return frame
            # Start of new frame
            self._buf.clear()
            self._in_frame = True
            self._esc = False
            return None

        if not self._in_frame:
            return None

        if byte == SLIP_ESC:
            self._esc = True
            return None

        if self._esc:
            self._esc = False
            byte = 0xC0 if byte == SLIP_ESC_END else 0xDB
        
        self._buf.append(byte)
        return None
```

Each byte from the serial port passes through `feed()`. When a complete SLIP frame arrives, `feed()` returns the raw bytes; otherwise it returns `None`.

---

## Packet Builder

The Python side mirrors the C struct definitions using `struct.pack`:

```python
def build_move_arc(x_mm, y_mm, z_mm, i_mm, j_mm, k_mm,
                   feed_mm_s, plane, cw):
    payload = struct.pack('<fffffffBB',
                          x_mm, y_mm, z_mm,
                          i_mm, j_mm, k_mm,
                          feed_mm_s,
                          plane & 0xFF,
                          1 if cw else 0)
    return build_frame(PKT_MOVE_ARC, payload)

def build_frame(pkt_type, payload=b''):
    global _tx_seq
    seq = _tx_seq & 0xFF
    _tx_seq = (_tx_seq + 1) & 0xFF
    raw = bytes([pkt_type, seq]) + payload
    crc = crc16_ccitt(raw)
    return slip_encode(raw + struct.pack('<H', crc))
```

The `<fffffffBB` format string means: little-endian, 7 floats, 2 unsigned bytes. This exactly matches the C struct `ProtoPktMoveArc_t __attribute__((packed))`.

---

## WebSocket Command Dispatch

```python
async def websocket_handler(ws):
    _clients.add(ws)
    try:
        async for message in ws:
            data = json.loads(message)
            frame = _build_tx_frame(data)
            if frame and _serial_writer:
                _serial_writer.write(frame)
                await _serial_writer.drain()
    finally:
        _clients.discard(ws)

def _build_tx_frame(data):
    cmd = data.get('cmd', '')
    if cmd == 'move_arc':
        return pkt.build_move_arc(
            float(data['x']), float(data['y']), float(data['z']),
            float(data['i']), float(data['j']), float(data['k']),
            float(data['feed']),
            int(data.get('plane', 0)),
            bool(data.get('cw', True)),
        )
    if cmd == 'move_g00':
        return pkt.build_move_g00(
            float(data['x']), float(data['y']), float(data['z']))
    # ... other commands
```

---

## Broadcasting Status to All Clients

Multiple browser tabs can connect simultaneously. Every STATUS packet from the MCU is broadcast to all connected clients:

```python
async def _broadcast(message: str):
    dead = set()
    for ws in _clients:
        try:
            await ws.send(message)
        except websockets.ConnectionClosed:
            dead.add(ws)
    _clients -= dead

# In serial_reader():
if pkt_type == PKT_STATUS:
    s = parse_status(payload)
    if s:
        await _broadcast(json.dumps(s.to_dict()))
```

The STATUS JSON looks like:
```json
{
  "type": "status",
  "ts": 1234567.89,
  "axes": [
    {"pos_mm": -981.23, "velocity": 0, "torque": 45, "cia402": "OP_ENABLED",
     "target_reached": true, "run_enable": true},
    {"pos_mm": -354.01, ...},
    {"pos_mm": -50.00,  ...}
  ],
  "interp": "IDLE",
  "all_ready": true,
  "all_targets_reached": true
}
```

---

## COM Port Discovery

The bridge exposes an HTTP endpoint for the HMI to discover available COM ports:

```python
class _PortsHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/ports':
            ports = [p.device for p in serial.tools.list_ports.comports()]
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(json.dumps(ports).encode())
```

The Web HMI fetches this list and populates a COM port dropdown. Much better than hardcoding `COM32`.

---

## Reconnection Logic

USB-UART adapters can disconnect and reconnect. The bridge handles this gracefully:

```python
async def serial_reconnect():
    while True:
        try:
            reader, writer = await serial_asyncio.open_serial_connection(
                url=_current_port, baudrate=_baud_rate)
            _serial_writer = writer
            await _broadcast(json.dumps({'type': 'connected',
                                         'port': _current_port}))
            await serial_reader(reader)   # runs until disconnect
        except Exception as e:
            log.warning("Serial error: %s — retrying in %.1fs", e, _reconnect_delay)
            await asyncio.sleep(_reconnect_delay)
```

After reconnect, the HMI automatically re-requests parameters and re-displays the current drive state without any user action.

---

## Next Post

The bridge is done. Now the front end: the **Web HMI** — a single HTML file with a real-time DRO, parameter editor, JOG controls, and G-code console, all in plain JavaScript with no framework.
