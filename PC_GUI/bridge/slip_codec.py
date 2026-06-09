"""
slip_codec.py — SLIP framing codec (RFC 1055).

Wire constants match uart_protocol.h:
  END      = 0xC0  (frame boundary)
  ESC      = 0xDB
  ESC_END  = 0xDC  (escaped 0xC0 inside frame)
  ESC_ESC  = 0xDD  (escaped 0xDB inside frame)
"""

SLIP_END     = 0xC0
SLIP_ESC     = 0xDB
SLIP_ESC_END = 0xDC
SLIP_ESC_ESC = 0xDD


def slip_encode(data: bytes) -> bytes:
    """Encode raw bytes as a SLIP frame with leading and trailing END byte."""
    out = bytearray()
    out.append(SLIP_END)
    for b in data:
        if b == SLIP_END:
            out.append(SLIP_ESC)
            out.append(SLIP_ESC_END)
        elif b == SLIP_ESC:
            out.append(SLIP_ESC)
            out.append(SLIP_ESC_ESC)
        else:
            out.append(b)
    out.append(SLIP_END)
    return bytes(out)


class SlipDecoder:
    """
    Stateful, streaming SLIP decoder.

    Usage:
        decoder = SlipDecoder()
        decoder.feed(chunk)           # feed any number of bytes
        for frame in decoder.frames():
            process(frame)            # complete decoded frame (no SLIP delimiters)
    """

    def __init__(self) -> None:
        self._buf: bytearray = bytearray()
        self._esc: bool = False
        self._active: bool = False       # True once first END byte seen
        self._frames: list[bytes] = []

    def feed(self, data: bytes) -> None:
        """Feed a chunk of raw serial bytes into the decoder."""
        for b in data:
            self._feed_byte(b)

    def _feed_byte(self, b: int) -> None:
        if b == SLIP_END:
            if self._active and self._buf:
                self._frames.append(bytes(self._buf))
            # reset decoder — ready for next frame
            self._buf.clear()
            self._esc = False
            self._active = True
            return

        if not self._active:
            return  # discard bytes before first END

        if self._esc:
            self._esc = False
            if b == SLIP_ESC_END:
                b = SLIP_END
            elif b == SLIP_ESC_ESC:
                b = SLIP_ESC
            # else: malformed escape — drop byte silently
        elif b == SLIP_ESC:
            self._esc = True
            return

        self._buf.append(b)

    def frames(self) -> list[bytes]:
        """Return and clear all complete frames decoded since the last call."""
        out, self._frames = self._frames, []
        return out

    def reset(self) -> None:
        """Reset decoder state (call after serial reconnect)."""
        self._buf.clear()
        self._esc = False
        self._active = False
        self._frames = []
