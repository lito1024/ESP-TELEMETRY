#!/usr/bin/env python3
"""Host tests for Stage 42A's UDPTransport (TelemetryUDPTransport.h).

Python mirror of the real C++ logic, matching the existing convention for
this project's transport-layer tests (see run_transport_tests.py). This
suite does not open real sockets -- it models UDPTransport's documented
send/receive/statistics contract (single-slot receive buffer, sendTo()
requiring a valid peer IP, sender IP exposed via receivePacket()) the same
way MockTransport in run_transport_tests.py mirrors ESPNowTransport/
UARTTransport's contract.
"""
from __future__ import annotations


class MockUDPTransport:
    def __init__(self) -> None:
        self.initialized = False
        self.local_port = 0
        self.queued: tuple[int, int, bytes] | None = None  # (sender_ip, sender_port, data)
        self.stats = {
            "packetsSent": 0,
            "packetsReceived": 0,
            "bytesSent": 0,
            "bytesReceived": 0,
            "sendFailures": 0,
            "receiveFailures": 0,
        }

    def set_local_port(self, port: int) -> None:
        self.local_port = port

    def begin(self) -> bool:
        self.initialized = True
        return True

    def send_to(self, peer_ip: int, peer_port: int, payload: bytes) -> bool:
        if not self.initialized or not payload or peer_ip == 0 or len(payload) > 256:
            self.stats["sendFailures"] += 1
            return False
        self.stats["packetsSent"] += 1
        self.stats["bytesSent"] += len(payload)
        return True

    def inject(self, sender_ip: int, sender_port: int, payload: bytes) -> None:
        # Mirrors pollForDatagram_(): a second datagram arriving before the
        # first is drained does NOT overwrite it -- it simply isn't polled
        # yet (unlike ESPNowTransport's callback-driven single-slot
        # overwrite). The mock only models one pending slot for simplicity,
        # matching what a single receivePacket() call actually drains.
        if self.queued is None:
            self.queued = (sender_ip, sender_port, payload)
            self.stats["packetsReceived"] += 1
            self.stats["bytesReceived"] += len(payload)

    def available(self) -> int:
        return len(self.queued[2]) if self.queued else 0

    def receive_packet(self) -> tuple[int, int, bytes] | None:
        if self.queued is None:
            return None
        packet = self.queued
        self.queued = None
        return packet


def test_begin_marks_initialized() -> None:
    transport = MockUDPTransport()
    assert not transport.initialized
    assert transport.begin()
    assert transport.initialized


def test_send_to_before_begin_fails() -> None:
    transport = MockUDPTransport()
    assert not transport.send_to(0x0102A8C0, 50823, b"frame")
    assert transport.stats["sendFailures"] == 1


def test_send_to_requires_nonzero_peer_ip() -> None:
    transport = MockUDPTransport()
    transport.begin()
    assert not transport.send_to(0, 50823, b"frame")
    assert transport.stats["sendFailures"] == 1


def test_send_to_statistics() -> None:
    transport = MockUDPTransport()
    transport.begin()
    assert transport.send_to(0x0102A8C0, 50823, b"abc")
    assert transport.stats["packetsSent"] == 1
    assert transport.stats["bytesSent"] == 3
    assert transport.stats["sendFailures"] == 0


def test_send_to_rejects_oversized_payload() -> None:
    transport = MockUDPTransport()
    transport.begin()
    assert not transport.send_to(0x0102A8C0, 50823, bytes(257))
    assert transport.stats["sendFailures"] == 1


def test_receive_exposes_sender_ip_and_port() -> None:
    transport = MockUDPTransport()
    transport.begin()
    transport.inject(0x0102A8C0, 12345, b"frame")
    packet = transport.receive_packet()
    assert packet is not None
    sender_ip, sender_port, data = packet
    assert sender_ip == 0x0102A8C0
    assert sender_port == 12345
    assert data == b"frame"
    assert transport.stats["packetsReceived"] == 1
    assert transport.stats["bytesReceived"] == 5


def test_receive_packet_drains_single_slot() -> None:
    transport = MockUDPTransport()
    transport.begin()
    transport.inject(0x0102A8C0, 1, b"first")
    assert transport.receive_packet() is not None
    assert transport.receive_packet() is None
    assert transport.available() == 0


def test_available_reflects_queued_length() -> None:
    transport = MockUDPTransport()
    transport.begin()
    assert transport.available() == 0
    transport.inject(0x0102A8C0, 1, b"12345")
    assert transport.available() == 5


def main() -> int:
    tests = [
        test_begin_marks_initialized,
        test_send_to_before_begin_fails,
        test_send_to_requires_nonzero_peer_ip,
        test_send_to_statistics,
        test_send_to_rejects_oversized_payload,
        test_receive_exposes_sender_ip_and_port,
        test_receive_packet_drains_single_slot,
        test_available_reflects_queued_length,
    ]
    for test in tests:
        test()
        print(f"{test.__name__}: OK")
    print(f"{len(tests)} UDP transport tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
