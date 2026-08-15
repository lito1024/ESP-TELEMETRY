#!/usr/bin/env python3
from __future__ import annotations

import re
import struct
from pathlib import Path

MAGIC = b"SPDP"
VERSION = 1
VERSION_V2 = 2
TYPE_SNAPSHOT = 1
TYPE_FAST = 16
HEADER_SIZE = 14
SNAPSHOT_PAYLOAD_SIZE = 32
TELEMETRY_PAYLOAD_HEADER_SIZE = 5
CRC_SIZE = 2
MAX_PAYLOAD_SIZE = 128
FIELD_PV1_POWER = 1001
FIELD_BATTERY_SOC = 2001
TYPE_DIAGNOSTICS = 19
# DIAGNOSTICS-group production field, see ESP-LUXPOWER/data/luxpower_entities.json entities[].
FIELD_BATTERY_CAPACITY = 9001
# Stage 30.1: field IDs at the first/middle/last position of the 79-field
# extended-diagnostics rotation (9006..9084), used to prove multi-batch
# delivery across interleaved sequence numbers.
FIELD_PV1_VOLTAGE = 9006  # rotation index 0 -> batch 0
FIELD_ENERGY_TO_GRID_TOTAL = 9047  # rotation index 41 -> batch 2
FIELD_GENERATOR_ENERGY_TODAY = 9084  # rotation index 78 -> batch 3 (last)
# Stage 31: catalog-driven update classes. FAST->FAST_TELEMETRY(16),
# NORMAL->STATUS(18), SLOW->DIAGNOSTICS(19).
TYPE_STATUS = 18
FIELD_LOAD_POWER = 9085
# Stage 36: Controller -> Gateway commands and Gateway -> Controller command
# results. See ESP-TELEMETRY/src/ESPTelemetry.h.
TYPE_COMMAND = 32
TYPE_COMMAND_RESULT = 33
COMMAND_PAYLOAD_SIZE = 8
COMMAND_RESULT_PAYLOAD_SIZE = 17
CMD_SET_MAX_BACKFLOW_POWER = 1
CMD_SET_ZERO_EXPORT = 2
RESULT_SUCCESS = 0
RESULT_SUCCESS_ALREADY_SET = 1
RESULT_TIMEOUT = 2
RESULT_COMMUNICATION_ERROR = 3
RESULT_VALIDATION_ERROR = 4
RESULT_READBACK_FAILED = 5
RESULT_UNKNOWN_DEVICE = 6
RESULT_UNSUPPORTED_COMMAND = 7
DEVICE_CONTROLLER_SYSTEM = 3
DEVICE_DISPLAY = 5
FIELD_DISPLAY_MAINTENANCE_WIFI_ACTUAL = 12001
FIELDS_HEADER = Path(__file__).resolve().parent.parent / "src" / "ESPTelemetryFields.h"

SYSTEM_MEASUREMENT_FIELDS = [
    ("system_output_voltage", "System Output Voltage", 10001, "V", 10),
    ("system_output_current", "System Output Current", 10002, "A", 100),
    ("system_output_power", "System Output Power", 10003, "W", 1),
    ("system_output_energy", "System Output Energy", 10004, "kWh", 100),
    ("system_output_frequency", "System Output Frequency", 10005, "Hz", 10),
    ("system_output_power_factor", "System Output Power Factor", 10006, "", 100),
    ("grid_input_voltage", "Grid Input Voltage", 10007, "V", 10),
    ("grid_input_current", "Grid Input Current", 10008, "A", 100),
    ("grid_input_power", "Grid Input Power", 10009, "W", 1),
    ("grid_input_energy", "Grid Input Energy", 10010, "kWh", 100),
    ("grid_input_frequency", "Grid Input Frequency", 10011, "Hz", 10),
    ("grid_input_power_factor", "Grid Input Power Factor", 10012, "", 100),
    ("grid_raw_voltage", "Grid Raw Voltage", 10013, "V", 10),
    ("display_enabled", "Display Enabled", 10014, "", 1),
]


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
    return struct.pack("<iiHiiIH8s", pv1, pv2, soc_x10, charge, discharge, energy_wh, flags, b"\x01" + b"\x00" * 7)


def encode(seq: int, timestamp: int, version: int = VERSION, payload: bytes | None = None) -> bytes:
    if payload is None:
        payload = snapshot_payload()
    header = MAGIC + bytes([version, TYPE_SNAPSHOT]) + struct.pack("<HHI", len(payload), seq, timestamp)
    frame_without_crc = header + payload
    return frame_without_crc + struct.pack("<H", crc16(frame_without_crc))


def encode_v2(seq: int, timestamp: int, fields: list[tuple[int, int]], msg_type: int = TYPE_FAST, device_id: int = 1) -> bytes:
    payload = struct.pack("<BBBH", device_id, 1, len(fields), 0x0003)
    for field_id, value in fields:
        payload += struct.pack("<Hi", field_id, value)
    header = MAGIC + bytes([VERSION_V2, msg_type]) + struct.pack("<HHI", len(payload), seq, timestamp)
    frame_without_crc = header + payload
    return frame_without_crc + struct.pack("<H", crc16(frame_without_crc))


def encode_command(seq: int, timestamp: int, device_id: int, command_id: int, command_sequence: int, value: int) -> bytes:
    payload = struct.pack("<BBHi", device_id, command_id, command_sequence, value)
    header = MAGIC + bytes([VERSION_V2, TYPE_COMMAND]) + struct.pack("<HHI", len(payload), seq, timestamp)
    frame_without_crc = header + payload
    return frame_without_crc + struct.pack("<H", crc16(frame_without_crc))


def encode_command_result(
    seq: int,
    timestamp: int,
    device_id: int,
    command_id: int,
    command_sequence: int,
    result_code: int,
    requested_value: int,
    previous_value: int,
    readback_value: int,
) -> bytes:
    payload = struct.pack(
        "<BBHBiii", device_id, command_id, command_sequence, result_code, requested_value, previous_value, readback_value
    )
    header = MAGIC + bytes([VERSION_V2, TYPE_COMMAND_RESULT]) + struct.pack("<HHI", len(payload), seq, timestamp)
    frame_without_crc = header + payload
    return frame_without_crc + struct.pack("<H", crc16(frame_without_crc))


def decode_command_fields(frame: bytes) -> dict[str, int]:
    payload_length = struct.unpack_from("<H", frame, 6)[0]
    payload = frame[HEADER_SIZE : HEADER_SIZE + payload_length]
    device_id, command_id, command_sequence, value = struct.unpack_from("<BBHi", payload, 0)
    return {"device_id": device_id, "command_id": command_id, "command_sequence": command_sequence, "value": value}


def decode_command_result_fields(frame: bytes) -> dict[str, int]:
    payload_length = struct.unpack_from("<H", frame, 6)[0]
    payload = frame[HEADER_SIZE : HEADER_SIZE + payload_length]
    device_id, command_id, command_sequence, result_code, requested, previous, readback = struct.unpack_from(
        "<BBHBiii", payload, 0
    )
    return {
        "device_id": device_id,
        "command_id": command_id,
        "command_sequence": command_sequence,
        "result_code": result_code,
        "requested_value": requested,
        "previous_value": previous,
        "readback_value": readback,
    }


class Decoder:
    def __init__(self) -> None:
        self.buffer = bytearray()
        self.magic_index = 0
        self.expected_length = 0
        self.last_sequence: dict[tuple[int, int], int] = {}

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
            if version not in (VERSION, VERSION_V2):
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
            version = frame[4]
            message_type = frame[5]
            payload = frame[HEADER_SIZE:HEADER_SIZE + struct.unpack_from("<H", frame, 6)[0]]
            # Stage 32: mirrors ESPTelemetry::Decoder::finishFrame -- key the
            # sequence tracker by (version, messageType, deviceId), not just
            # (version, messageType), so two interleaved sources don't get
            # compared against each other's sequence counters.
            device_id = 0
            if (
                version == VERSION_V2
                and message_type in (TYPE_FAST, TYPE_STATUS, TYPE_DIAGNOSTICS, 17, TYPE_COMMAND, TYPE_COMMAND_RESULT)
                and payload
            ):
                device_id = payload[0]
            elif version == VERSION and message_type == TYPE_SNAPSHOT and len(payload) >= SNAPSHOT_PAYLOAD_SIZE:
                device_id = payload[24]
            stream = (version, message_type, device_id)
            if stream in self.last_sequence and sequence != ((self.last_sequence[stream] + 1) & 0xFFFF):
                self.last_sequence[stream] = sequence
                return "SequenceGap"
            self.last_sequence[stream] = sequence
            return "FrameReady"
        return None


def feed_all(decoder: Decoder, data: bytes):
    results = []
    for byte in data:
        result = decoder.feed(byte)
        if result is not None:
            results.append(result)
    return results


def decode_v2_fields(frame: bytes) -> dict[int, int]:
    """Parse a v2 telemetry frame's field records directly from wire bytes
    (independent of Decoder/sequence state) so tests can assert on actual
    field content, not just the decode result label."""
    payload_length = struct.unpack_from("<H", frame, 6)[0]
    payload = frame[HEADER_SIZE : HEADER_SIZE + payload_length]
    field_count = payload[2]
    fields = {}
    for i in range(field_count):
        offset = TELEMETRY_PAYLOAD_HEADER_SIZE + i * 6
        field_id, value = struct.unpack_from("<Hi", payload, offset)
        fields[field_id] = value
    return fields


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


def test_v2_valid_frame() -> None:
    frame = encode_v2(1, 100, [(FIELD_PV1_POWER, 2276), (FIELD_BATTERY_SOC, 1000)])
    assert feed_all(Decoder(), frame) == ["FrameReady"]


def test_back_to_back_mixed_v1_v2() -> None:
    assert feed_all(Decoder(), encode(0, 100) + encode_v2(0, 200, [(FIELD_PV1_POWER, 123)])) == [
        "FrameReady",
        "FrameReady",
    ]


def test_unknown_v2_message_type_skips_after_crc() -> None:
    frame = encode_v2(1, 100, [(9999, 1)], msg_type=99)
    assert feed_all(Decoder(), frame) == ["FrameReady"]


def test_v2_diagnostics_frame_decodes() -> None:
    # The DIAGNOSTICS message type must decode exactly like any other v2
    # group at the protocol layer.
    frame = encode_v2(1, 100, [(FIELD_BATTERY_CAPACITY, 142)], msg_type=TYPE_DIAGNOSTICS)
    assert feed_all(Decoder(), frame) == ["FrameReady"]


def test_multi_batch_diagnostics_first_middle_last_all_arrive() -> None:
    # Stage 30.1: the real Gateway shares ONE global sequence counter across
    # FAST/STATUS/ENERGY/DIAGNOSTICS/extended-diagnostics sends (see
    # luxpower-gateway/src/main.cpp nextV2Sequence). That means the 4 rotating
    # extended-diagnostics batches arrive on the wire with NON-consecutive
    # sequence numbers on the DIAGNOSTICS (version=2, type=19) stream --
    # other message types' sends increment the shared counter in between.
    # This must not cause any batch to be dropped or its fields lost: the
    # decoder should still report FrameReady/SequenceGap (never BadCrc/
    # UnsupportedVersion/PayloadTooLarge) and the fields must be recoverable.
    decoder = Decoder()
    # Sequence numbers deliberately non-consecutive on this stream, mimicking
    # interleaved FAST/STATUS/ENERGY sends between each DIAGNOSTICS send.
    batch0 = encode_v2(10, 1000, [(FIELD_PV1_VOLTAGE, 2314)], msg_type=TYPE_DIAGNOSTICS)
    batch1 = encode_v2(15, 2000, [(FIELD_BATTERY_CAPACITY, 142)], msg_type=TYPE_DIAGNOSTICS)
    batch2 = encode_v2(23, 3000, [(FIELD_ENERGY_TO_GRID_TOTAL, 5678)], msg_type=TYPE_DIAGNOSTICS)
    batch3 = encode_v2(30, 4000, [(FIELD_GENERATOR_ENERGY_TODAY, 91)], msg_type=TYPE_DIAGNOSTICS)

    results = []
    fields_seen = {}
    for batch in (batch0, batch1, batch2, batch3):
        result = feed_all(decoder, batch)
        assert result == ["FrameReady"] or result == ["SequenceGap"], result
        results.append(result[0])
        fields_seen.update(decode_v2_fields(batch))

    # None of the 4 batches were dropped as BadCrc/UnsupportedVersion/etc.
    assert all(r in ("FrameReady", "SequenceGap") for r in results)
    # First, middle, and last rotation positions all decode with correct values.
    assert fields_seen[FIELD_PV1_VOLTAGE] == 2314
    assert fields_seen[FIELD_ENERGY_TO_GRID_TOTAL] == 5678
    assert fields_seen[FIELD_GENERATOR_ENERGY_TODAY] == 91


def test_repeated_diagnostics_type_frames_all_independently_accepted() -> None:
    # Multiple frames sharing the same (version, message_type) are NOT
    # treated as duplicates just because the type repeats -- each is its
    # own payload and must be decoded independently as long as its own
    # sequence/CRC are valid.
    decoder = Decoder()
    data = (
        encode_v2(1, 100, [(FIELD_PV1_VOLTAGE, 100)], msg_type=TYPE_DIAGNOSTICS)
        + encode_v2(2, 200, [(FIELD_ENERGY_TO_GRID_TOTAL, 200)], msg_type=TYPE_DIAGNOSTICS)
        + encode_v2(3, 300, [(FIELD_GENERATOR_ENERGY_TODAY, 300)], msg_type=TYPE_DIAGNOSTICS)
    )
    assert feed_all(decoder, data) == ["FrameReady", "FrameReady", "FrameReady"]


def test_startup_burst_first_middle_last_batch_all_classes_decode() -> None:
    # Stage 31 section 8: the startup/recovery burst sends FAST, NORMAL, and
    # SLOW batches back-to-back immediately after the first valid cache.
    # Simulate a representative burst: one FAST_TELEMETRY frame (first), one
    # STATUS (NORMAL) frame (middle), two DIAGNOSTICS (SLOW) frames (last
    # two, since SLOW has 32 fields > 20 per frame). Every frame must decode
    # and every field must be recoverable, with strictly increasing
    # sequence numbers (the real Gateway's shared nextV2Sequence counter).
    decoder = Decoder()
    fast_frame = encode_v2(100, 1000, [(FIELD_PV1_POWER, 500), (FIELD_LOAD_POWER, 320)], msg_type=TYPE_FAST)
    normal_frame = encode_v2(101, 1001, [(FIELD_PV1_VOLTAGE, 2350)], msg_type=TYPE_STATUS)
    slow_frame_1 = encode_v2(102, 1002, [(FIELD_BATTERY_CAPACITY, 280)], msg_type=TYPE_DIAGNOSTICS)
    slow_frame_2 = encode_v2(103, 1003, [(FIELD_ENERGY_TO_GRID_TOTAL, 9999)], msg_type=TYPE_DIAGNOSTICS)

    fields_seen: dict[int, int] = {}
    results = []
    for frame in (fast_frame, normal_frame, slow_frame_1, slow_frame_2):
        result = feed_all(decoder, frame)
        assert result == ["FrameReady"], result
        results.append(result[0])
        fields_seen.update(decode_v2_fields(frame))

    assert results == ["FrameReady", "FrameReady", "FrameReady", "FrameReady"]
    assert fields_seen[FIELD_PV1_POWER] == 500  # first (FAST)
    assert fields_seen[FIELD_LOAD_POWER] == 320  # first (FAST)
    assert fields_seen[FIELD_PV1_VOLTAGE] == 2350  # middle (NORMAL/STATUS)
    assert fields_seen[FIELD_BATTERY_CAPACITY] == 280  # last group, frame 1 (SLOW/DIAGNOSTICS)
    assert fields_seen[FIELD_ENERGY_TO_GRID_TOTAL] == 9999  # last group, frame 2 (SLOW/DIAGNOSTICS)


def test_interleaved_two_device_sequences_do_not_conflate() -> None:
    # Stage 32: a single Decoder instance (as used by the Display's UART
    # receiver, fed a merged stream from two Gateways forwarded through one
    # Bridge) must track device 1's and device 2's FAST_TELEMETRY sequence
    # numbers independently. Before the Stage 32 fix, both devices shared one
    # (version, messageType) tracker, so this exact interleaving pattern
    # would have produced spurious SequenceGap results on frames 2 and 4.
    decoder = Decoder()
    d1_frame1 = encode_v2(1, 1000, [(FIELD_PV1_POWER, 100)], msg_type=TYPE_FAST, device_id=1)
    d2_frame1 = encode_v2(1, 1000, [(FIELD_PV1_POWER, 200)], msg_type=TYPE_FAST, device_id=2)
    d1_frame2 = encode_v2(2, 1001, [(FIELD_PV1_POWER, 101)], msg_type=TYPE_FAST, device_id=1)
    d2_frame2 = encode_v2(2, 1001, [(FIELD_PV1_POWER, 201)], msg_type=TYPE_FAST, device_id=2)

    results = []
    for frame in (d1_frame1, d2_frame1, d1_frame2, d2_frame2):
        results += feed_all(decoder, frame)

    assert results == ["FrameReady", "FrameReady", "FrameReady", "FrameReady"], results


def test_real_gap_still_detected_per_device_when_interleaved() -> None:
    # Companion to the above: a genuine gap in device 1's stream must still
    # be caught even while device 2's independent stream keeps advancing
    # normally in between.
    decoder = Decoder()
    d1_frame1 = encode_v2(1, 1000, [(FIELD_PV1_POWER, 100)], msg_type=TYPE_FAST, device_id=1)
    d2_frame1 = encode_v2(1, 1000, [(FIELD_PV1_POWER, 200)], msg_type=TYPE_FAST, device_id=2)
    d1_frame_gap = encode_v2(5, 1001, [(FIELD_PV1_POWER, 101)], msg_type=TYPE_FAST, device_id=1)  # skipped 2,3,4
    d2_frame2 = encode_v2(2, 1001, [(FIELD_PV1_POWER, 201)], msg_type=TYPE_FAST, device_id=2)

    results = []
    for frame in (d1_frame1, d2_frame1, d1_frame_gap, d2_frame2):
        results += feed_all(decoder, frame)

    assert results == ["FrameReady", "FrameReady", "SequenceGap", "FrameReady"], results


def test_command_encode_decode_roundtrip() -> None:
    frame = encode_command(1, 100, device_id=2, command_id=CMD_SET_MAX_BACKFLOW_POWER, command_sequence=7, value=70)
    assert feed_all(Decoder(), frame) == ["FrameReady"]
    fields = decode_command_fields(frame)
    assert fields == {"device_id": 2, "command_id": CMD_SET_MAX_BACKFLOW_POWER, "command_sequence": 7, "value": 70}


def test_command_result_encode_decode_roundtrip() -> None:
    frame = encode_command_result(
        1,
        100,
        device_id=2,
        command_id=CMD_SET_ZERO_EXPORT,
        command_sequence=7,
        result_code=RESULT_SUCCESS,
        requested_value=1,
        previous_value=0,
        readback_value=1,
    )
    assert feed_all(Decoder(), frame) == ["FrameReady"]
    fields = decode_command_result_fields(frame)
    assert fields == {
        "device_id": 2,
        "command_id": CMD_SET_ZERO_EXPORT,
        "command_sequence": 7,
        "result_code": RESULT_SUCCESS,
        "requested_value": 1,
        "previous_value": 0,
        "readback_value": 1,
    }


def test_command_negative_value_roundtrips_signed() -> None:
    # value is a signed int32 on the wire (matches TelemetryFieldValue's
    # existing convention) -- not currently used by either allowlisted
    # command (both are non-negative), but the codec itself must not
    # truncate or misinterpret a negative value if ever needed.
    frame = encode_command(1, 100, device_id=1, command_id=CMD_SET_MAX_BACKFLOW_POWER, command_sequence=1, value=-5)
    fields = decode_command_fields(frame)
    assert fields["value"] == -5


def test_command_device_ids_sequenced_independently() -> None:
    # Same requirement as telemetry (Stage 32): commands to device 1 and
    # device 2 must not be compared against each other's sequence counters.
    decoder = Decoder()
    d1 = encode_command(1, 1000, device_id=1, command_id=CMD_SET_MAX_BACKFLOW_POWER, command_sequence=1, value=50)
    d2 = encode_command(1, 1000, device_id=2, command_id=CMD_SET_MAX_BACKFLOW_POWER, command_sequence=1, value=50)
    d1_next = encode_command(2, 1001, device_id=1, command_id=CMD_SET_MAX_BACKFLOW_POWER, command_sequence=2, value=60)
    results = []
    for frame in (d1, d2, d1_next):
        results += feed_all(decoder, frame)
    assert results == ["FrameReady", "FrameReady", "FrameReady"], results


def test_command_and_telemetry_do_not_share_sequence_stream() -> None:
    # TYPE_COMMAND and TYPE_FAST are different message_type values, so
    # sharing one Decoder instance (as Bridge does, decoding both directions)
    # must not conflate a Gateway's telemetry sequence with a Controller's
    # command sequence for the same device_id.
    decoder = Decoder()
    telemetry = encode_v2(1, 1000, [(FIELD_PV1_POWER, 100)], msg_type=TYPE_FAST, device_id=2)
    command = encode_command(1, 1000, device_id=2, command_id=CMD_SET_ZERO_EXPORT, command_sequence=1, value=1)
    results = []
    for frame in (telemetry, command):
        results += feed_all(decoder, frame)
    assert results == ["FrameReady", "FrameReady"], results


def test_controller_system_device_id_sequences_independent_from_inverters() -> None:
    decoder = Decoder()
    inv1 = encode_v2(1, 1000, [(FIELD_PV1_POWER, 100)], msg_type=TYPE_FAST, device_id=1)
    inv2 = encode_v2(1, 1000, [(FIELD_PV1_POWER, 200)], msg_type=TYPE_FAST, device_id=2)
    system = encode_v2(1, 1000, [(10003, 1234)], msg_type=TYPE_FAST, device_id=DEVICE_CONTROLLER_SYSTEM)
    results = []
    for frame in (inv1, inv2, system):
        results += feed_all(decoder, frame)
    assert results == ["FrameReady", "FrameReady", "FrameReady"], results


def test_stage40a_display_status_field_and_device_id() -> None:
    decoder = Decoder()
    frame = encode_v2(
        1,
        1000,
        [(FIELD_DISPLAY_MAINTENANCE_WIFI_ACTUAL, 0)],
        msg_type=TYPE_STATUS,
        device_id=DEVICE_DISPLAY,
    )
    assert feed_all(decoder, frame) == ["FrameReady"]

    source = FIELDS_HEADER.read_text(encoding="utf-8")
    assert "FieldIdDisplayMaintenanceWifiActual = 12001" in source
    assert '{FieldIdDisplayMaintenanceWifiActual, "display_maintenance_wifi_actual", "Display Maintenance Wi-Fi Actual", "", 1, TelemetryGroup::Status' in source


def test_stage39a_system_measurement_field_descriptors_are_final_not_simulated() -> None:
    source = FIELDS_HEADER.read_text(encoding="utf-8")
    enum_ids = {
        name: int(value)
        for name, value in re.findall(r"(FieldId\w+)\s*=\s*(\d+)", source)
    }
    descriptor_pattern = re.compile(
        r"\{FieldId(?P<enum>\w+),\s+\"(?P<key>[^\"]+)\",\s+\"(?P<name>[^\"]+)\",\s+\"(?P<unit>[^\"]*)\",\s+(?P<scale>\d+),"
    )
    descriptors = {}
    for match in descriptor_pattern.finditer(source):
        enum_name = "FieldId" + match.group("enum")
        descriptors[match.group("key")] = (
            enum_ids[enum_name],
            match.group("name"),
            match.group("unit"),
            int(match.group("scale")),
        )

    ids = [field_id for _key, _name, field_id, _unit, _scale in SYSTEM_MEASUREMENT_FIELDS]
    assert len(ids) == len(set(ids))
    assert not set(ids).intersection({0, 1, 2, 1001, 1002, 2001, 2002, 2003, 5001, 7001, 7002, 7003})
    for key, name, field_id, unit, scale in SYSTEM_MEASUREMENT_FIELDS:
        assert descriptors[key] == (field_id, name, unit, scale)
        combined = f"{key} {name}".lower()
        assert "sim" not in combined
        assert "test" not in combined
        assert "pzem" not in combined
        assert "lm358" not in combined


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
        test_v2_valid_frame,
        test_back_to_back_mixed_v1_v2,
        test_unknown_v2_message_type_skips_after_crc,
        test_v2_diagnostics_frame_decodes,
        test_multi_batch_diagnostics_first_middle_last_all_arrive,
        test_repeated_diagnostics_type_frames_all_independently_accepted,
        test_startup_burst_first_middle_last_batch_all_classes_decode,
        test_interleaved_two_device_sequences_do_not_conflate,
        test_real_gap_still_detected_per_device_when_interleaved,
        test_command_encode_decode_roundtrip,
        test_command_result_encode_decode_roundtrip,
        test_command_negative_value_roundtrips_signed,
        test_command_device_ids_sequenced_independently,
        test_command_and_telemetry_do_not_share_sequence_stream,
        test_controller_system_device_id_sequences_independent_from_inverters,
        test_stage40a_display_status_field_and_device_id,
        test_stage39a_system_measurement_field_descriptors_are_final_not_simulated,
    ]
    for test in tests:
        test()
        print(f"{test.__name__}: OK")
    print(f"{len(tests)} protocol tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
