"""
TCP KISS client: injects a crafted TNC2 packet through the iGate's TCP KISS
server (src/tcp_kiss_utils.cpp, default port 8001) -- the iGate decodes it
and transmits it over LoRa RF exactly as if it originated the frame itself
(src/tcp_kiss_utils.cpp:50, LoRa_Utils::sendNewPacket(frame)).

Only starts once the iGate's WiFi STA is connected
(src/device_role.cpp:283, startNetworkServices() -- already gated by
pre-flight's WIFI_NOT_CONNECTED check, so no separate readiness probe here).

Self-loop guard on the firmware side (tcp_kiss_utils.cpp:46-51): it won't
retransmit if the decoded sender equals the iGate's own callsign -- callers
must use a fake sender that isn't cfg.igate_callsign.
"""

from __future__ import annotations

import socket

from ax25_kiss import encode_kiss_frame


class TCPKissClient:
    def __init__(self, host: str, port: int = 8001, timeout: float = 5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self._sock: socket.socket | None = None

    def connect(self) -> None:
        self._sock = socket.create_connection((self.host, self.port), timeout=self.timeout)

    def send_tnc2(self, tnc2_line: str) -> None:
        assert self._sock is not None, "TCPKissClient: not connected"
        self._sock.sendall(encode_kiss_frame(tnc2_line))

    def close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

    def __enter__(self) -> "TCPKissClient":
        self.connect()
        return self

    def __exit__(self, *exc) -> None:
        self.close()
