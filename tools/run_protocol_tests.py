#!/usr/bin/env python3
from __future__ import annotations

import struct

MAGIC = b"SPDP"
VERSION = 1
TYPE_SNAPSHOT = 1
HEADER_SIZE = 14
SNAPSHOT_PAYLOAD_SIZE = 32
CRC_SIZE = 2
MAX_PAYLOAD_SIZE = 64


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
            crc &= 0xFFFF
    return crc


def snapshot_payload(
    pv1: int = 2276,
    pv2: int = 1739,
    soc_x10: int = 1000,
    charge: int = 0,
    discharge: int = 0,
    energy_wh: int = 123456,
    flags: int = 0x0007,
) -> bytes:
    return struct.pack("<iiHiiIH8s", pv1, pv2, soc_x10, charge, discharge, energy_wh, flags, b"\x00" * 8)


def encode(seq: int, timestamp: int, version: int = VERSION, payload: bytes | None = None) -> bytes:
    if payload is None:
        payload = snapshot_payload()
    header = MAGIC + bytes([version, TYPE_SNAPSHOT]) + struct.pack("<HHI", len(payload), seq, timestamp)
    frame_without_crc = header + payload
    return frame_without_crc + struct.pack("<H", crc16(frame_without_crc))


class Decoder:
    def __init__(self) -> None:
        self.buffer = bytearray()
        self.magic_index = 0
        self.expected_length = 0
        self.last_sequence: int | None = None

    def feed(self, byte: int):
        if len(self.buffer) < len(MAGIC):
            expected = MAGIC[self.magic_index]
            if byte == expected:
                self.buffer.append(byte)
                self.magic_index += 1
                return None
            self.buffer.clear()
            self.magic_index = 1 if byte == MAGIC[0] else 0
            if self.magic_index:
                self.buffer.append(byte)
            return None

        self.buffer.append(byte)
        if len(self.buffer) == HEADER_SIZE:
            version = self.buffer[4]
            if version != VERSION:
                self.buffer.clear()
                self.magic_index = 0
                return "UnsupportedVersion"
            payload_length = struct.unpack_from("<H", self.buffer, 6)[0]
            if payload_length > MAX_PAYLOAD_SIZE:
                self.buffer.clear()
                self.magic_index = 0
                return "PayloadTooLarge"
            self.expected_length = HEADER_SIZE + payload_length + CRC_SIZE

        if self.expected_length and len(self.buffer) == self.expected_length:
            expected_crc = struct.unpack_from("<H", self.buffer, self.expected_length - CRC_SIZE)[0]
            actual_crc = crc16(bytes(self.buffer[:-CRC_SIZE]))
            frame = bytes(self.buffer)
            self.buffer.clear()
            self.magic_index = 0
            self.expected_length = 0
            if expected_crc != actual_crc:
                return "BadCrc"
            sequence = struct.unpack_from("<H", frame, 8)[0]
            if self.last_sequence is not None and sequence != ((self.last_sequence + 1) & 0xFFFF):
                self.last_sequence = sequence
                return "SequenceGap"
            self.last_sequence = sequence
            return "FrameReady"
        return None


def feed_all(decoder: Decoder, data: bytes):
    results = []
    for byte in data:
        result = decoder.feed(byte)
        if result is not None:
            results.append(result)
    return results


def test_crc() -> None:
    assert crc16(b"123456789") == 0x4B37


def test_bad_crc() -> None:
    frame = bytearray(encode(1, 100))
    frame[-1] ^= 0x55
    assert feed_all(Decoder(), bytes(frame)) == ["BadCrc"]


def test_valid_frame() -> None:
    assert feed_all(Decoder(), encode(1, 100)) == ["FrameReady"]


def test_noise_before_magic() -> None:
    assert feed_all(Decoder(), b"\x00noise" + encode(1, 100)) == ["FrameReady"]


def test_partial_frame() -> None:
    decoder = Decoder()
    frame = encode(1, 100)
    assert feed_all(decoder, frame[:10]) == []
    assert feed_all(decoder, frame[10:]) == ["FrameReady"]


def test_back_to_back_frames() -> None:
    data = encode(1, 100) + encode(2, 200)
    assert feed_all(Decoder(), data) == ["FrameReady", "FrameReady"]


def test_sequence_gap() -> None:
    data = encode(10, 100) + encode(12, 200)
    assert feed_all(Decoder(), data) == ["FrameReady", "SequenceGap"]


def test_unsupported_version() -> None:
    assert feed_all(Decoder(), encode(1, 100, version=99)) == ["UnsupportedVersion"]


def main() -> int:
    tests = [
        test_crc,
        test_valid_frame,
        test_bad_crc,
        test_noise_before_magic,
        test_partial_frame,
        test_back_to_back_frames,
        test_sequence_gap,
        test_unsupported_version,
    ]
    for test in tests:
        test()
        print(f"{test.__name__}: OK")
    print(f"{len(tests)} protocol tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
