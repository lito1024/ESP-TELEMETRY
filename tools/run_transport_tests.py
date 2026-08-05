#!/usr/bin/env python3
from __future__ import annotations


class MockTransport:
    def __init__(self) -> None:
        self.started = False
        self.rx: list[bytes] = []
        self.stats = {
            "packetsSent": 0,
            "packetsReceived": 0,
            "bytesSent": 0,
            "bytesReceived": 0,
            "sendFailures": 0,
            "receiveFailures": 0,
            "peerReady": False,
        }

    def begin(self) -> bool:
        self.started = True
        self.stats["peerReady"] = True
        return True

    def send(self, payload: bytes) -> bool:
        if not self.started or not payload:
            self.stats["sendFailures"] += 1
            return False
        self.stats["packetsSent"] += 1
        self.stats["bytesSent"] += len(payload)
        return True

    def inject(self, payload: bytes) -> None:
        self.rx.append(payload)

    def available(self) -> int:
        return len(self.rx[0]) if self.rx else 0

    def receive(self) -> bytes:
        if not self.rx:
            self.stats["receiveFailures"] += 1
            return b""
        payload = self.rx.pop(0)
        self.stats["packetsReceived"] += 1
        self.stats["bytesReceived"] += len(payload)
        return payload


def test_begin_marks_peer_ready() -> None:
    transport = MockTransport()
    assert transport.begin()
    assert transport.stats["peerReady"] is True


def test_send_statistics() -> None:
    transport = MockTransport()
    transport.begin()
    assert transport.send(b"abc")
    assert transport.stats["packetsSent"] == 1
    assert transport.stats["bytesSent"] == 3


def test_send_before_begin_fails() -> None:
    transport = MockTransport()
    assert not transport.send(b"abc")
    assert transport.stats["sendFailures"] == 1


def test_receive_statistics() -> None:
    transport = MockTransport()
    transport.inject(b"frame")
    assert transport.available() == 5
    assert transport.receive() == b"frame"
    assert transport.stats["packetsReceived"] == 1
    assert transport.stats["bytesReceived"] == 5


def main() -> int:
    tests = [
        test_begin_marks_peer_ready,
        test_send_statistics,
        test_send_before_begin_fails,
        test_receive_statistics,
    ]
    for test in tests:
        test()
        print(f"{test.__name__}: OK")
    print(f"{len(tests)} transport tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
