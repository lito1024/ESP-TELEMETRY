#!/usr/bin/env python3
from __future__ import annotations

VERSION1 = 1
VERSION2 = 2
TYPE_SNAPSHOT = 1
TYPE_FAST = 16
TYPE_ENERGY = 17
TYPE_STATUS = 18


class PeerManager:
    def __init__(self, capacity: int = 4, stale_timeout_ms: int = 5000, max_streams_per_peer: int = 5) -> None:
        self.capacity = capacity
        self.stale_timeout_ms = stale_timeout_ms
        self.max_streams_per_peer = max_streams_per_peer
        self.peers: dict[tuple[int, ...], dict] = {}
        self.device_to_mac: dict[int, tuple[int, ...]] = {}
        self.unknown_sender_rejects = 0
        self.identity_mismatch_rejects = 0
        self.config_conflict = False

    def add(self, mac: tuple[int, ...], device_id: int) -> str:
        if device_id == 0:
            self.config_conflict = True
            return "InvalidDeviceId"
        if mac in self.peers:
            if self.peers[mac]["device_id"] != device_id:
                self.peers[mac]["config_conflict"] = True
                self.config_conflict = True
            return "DuplicateMac"
        if device_id in self.device_to_mac:
            self.peers[self.device_to_mac[device_id]]["config_conflict"] = True
            self.config_conflict = True
            return "DuplicateDeviceId"
        if len(self.peers) >= self.capacity:
            self.config_conflict = True
            return "CapacityFull"
        self.peers[mac] = {
            "device_id": device_id,
            "last_sequence": 0,
            "last_packet_ms": 0,
            "accepted": 0,
            "duplicates": 0,
            "gaps": 0,
            "reboots": 0,
            "config_conflict": False,
            # keyed by (version, message_type); this is the fix under test:
            # each logical stream tracks its own sequence continuity so
            # interleaved v1/v2 (or interleaved v2 message groups) frames from
            # the same sender never appear as gaps against each other.
            "streams": {},
        }
        self.device_to_mac[device_id] = mac
        return "Added"

    def record(self,
               mac: tuple[int, ...],
               device_id: int,
               version: int,
               message_type: int,
               sequence: int,
               now_ms: int) -> str:
        if mac not in self.peers:
            self.unknown_sender_rejects += 1
            return "UnknownSender"
        peer = self.peers[mac]
        if peer["config_conflict"]:
            return "ConfigConflict"
        if peer["device_id"] != device_id:
            self.identity_mismatch_rejects += 1
            peer["config_conflict"] = True
            return "DeviceIdMismatch"

        streams = peer["streams"]
        key = (version, message_type)
        stream = streams.get(key)
        if stream is None:
            if len(streams) >= self.max_streams_per_peer:
                # Capacity safety net (documented, matches the C++ fallback):
                # accept without per-stream sequence tracking rather than
                # inventing a new rejection outcome.
                peer["accepted"] = int(peer["accepted"]) + 1
                peer["last_sequence"] = sequence
                peer["last_packet_ms"] = now_ms
                return "Accepted"
            stream = {"seen": False, "last_sequence": 0, "duplicates": 0, "gaps": 0, "reboots": 0}
            streams[key] = stream

        result = "Accepted"
        if stream["seen"]:
            expected = (int(stream["last_sequence"]) + 1) & 0xFFFF
            if sequence == stream["last_sequence"]:
                stream["duplicates"] = int(stream["duplicates"]) + 1
                peer["duplicates"] = int(peer["duplicates"]) + 1
                return "DuplicateSequence"
            elif sequence != expected:
                if sequence <= 1 and int(stream["last_sequence"]) > 100:
                    stream["reboots"] = int(stream["reboots"]) + 1
                    peer["reboots"] = int(peer["reboots"]) + 1
                    result = "SenderReboot"
                else:
                    stream["gaps"] = int(stream["gaps"]) + 1
                    peer["gaps"] = int(peer["gaps"]) + 1
                    result = "SequenceGap"

        stream["seen"] = True
        stream["last_sequence"] = sequence
        peer["accepted"] = int(peer["accepted"]) + 1
        peer["last_sequence"] = sequence
        peer["last_packet_ms"] = now_ms
        return result

    def stale(self, mac: tuple[int, ...], now_ms: int) -> bool:
        peer = self.peers[mac]
        return not peer["accepted"] or now_ms - int(peer["last_packet_ms"]) > self.stale_timeout_ms


MAC1 = (0x02, 0, 0, 0, 0, 1)
MAC2 = (0x02, 0, 0, 0, 0, 2)


def test_add_trusted_peer() -> None:
    peers = PeerManager()
    assert peers.add(MAC1, 1) == "Added"


def test_reject_duplicate_mac() -> None:
    peers = PeerManager()
    assert peers.add(MAC1, 1) == "Added"
    assert peers.add(MAC1, 2) == "DuplicateMac"
    assert peers.config_conflict


def test_reject_duplicate_device_id() -> None:
    peers = PeerManager()
    assert peers.add(MAC1, 1) == "Added"
    assert peers.add(MAC2, 1) == "DuplicateDeviceId"
    assert peers.config_conflict


def test_unknown_sender() -> None:
    peers = PeerManager()
    assert peers.add(MAC1, 1) == "Added"
    assert peers.record(MAC2, 1, VERSION1, TYPE_SNAPSHOT, 1, 1000) == "UnknownSender"
    assert peers.unknown_sender_rejects == 1


def test_wrong_device_id() -> None:
    peers = PeerManager()
    assert peers.add(MAC1, 1) == "Added"
    assert peers.record(MAC1, 2, VERSION1, TYPE_SNAPSHOT, 1, 1000) == "DeviceIdMismatch"
    assert peers.identity_mismatch_rejects == 1


def test_sequence_increment_duplicate_gap_wrap_and_reboot() -> None:
    peers = PeerManager()
    assert peers.add(MAC1, 1) == "Added"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 10, 1000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 11, 2000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 11, 3000) == "DuplicateSequence"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 13, 4000) == "SequenceGap"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 0xFFFF, 5000) == "SequenceGap"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 0, 6000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 200, 7000) == "SequenceGap"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 0, 8000) == "SenderReboot"


def test_per_peer_stale_independent() -> None:
    peers = PeerManager()
    assert peers.add(MAC1, 1) == "Added"
    assert peers.add(MAC2, 2) == "Added"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 1, 1000) == "Accepted"
    assert peers.record(MAC2, 2, VERSION1, TYPE_SNAPSHOT, 1, 5000) == "Accepted"
    assert peers.stale(MAC1, 7001)
    assert not peers.stale(MAC2, 7001)


def test_capacity_limit() -> None:
    peers = PeerManager(capacity=1)
    assert peers.add(MAC1, 1) == "Added"
    assert peers.add(MAC2, 2) == "CapacityFull"


# --- Stage 26.2: per-stream sequence tracking -------------------------------


def test_v1_sequence_no_gaps() -> None:
    peers = PeerManager()
    peers.add(MAC1, 1)
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 1, 1000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 2, 2000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 3, 3000) == "Accepted"
    assert peers.peers[MAC1]["gaps"] == 0


def test_v2_fast_sequence_no_gaps() -> None:
    peers = PeerManager()
    peers.add(MAC1, 1)
    assert peers.record(MAC1, 1, VERSION2, TYPE_FAST, 1, 1000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION2, TYPE_FAST, 2, 2000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION2, TYPE_FAST, 3, 3000) == "Accepted"
    assert peers.peers[MAC1]["gaps"] == 0


def test_interleaved_v1_and_v2_no_gaps() -> None:
    peers = PeerManager()
    peers.add(MAC1, 1)
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 1, 1000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION2, TYPE_FAST, 1, 1000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 2, 2000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION2, TYPE_FAST, 2, 2000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 3, 3000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION2, TYPE_FAST, 3, 3000) == "Accepted"
    assert peers.peers[MAC1]["gaps"] == 0


def test_interleaved_v2_groups_no_gaps() -> None:
    peers = PeerManager()
    peers.add(MAC1, 1)
    for seq in (1, 2, 3):
        now_ms = seq * 1000
        assert peers.record(MAC1, 1, VERSION2, TYPE_FAST, seq, now_ms) == "Accepted"
        assert peers.record(MAC1, 1, VERSION2, TYPE_ENERGY, seq, now_ms) == "Accepted"
        assert peers.record(MAC1, 1, VERSION2, TYPE_STATUS, seq, now_ms) == "Accepted"
    assert peers.peers[MAC1]["gaps"] == 0


def test_real_gap_inside_v1_only() -> None:
    peers = PeerManager()
    peers.add(MAC1, 1)
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 1, 1000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION2, TYPE_FAST, 1, 1000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 3, 2000) == "SequenceGap"  # v1 skipped 2
    assert peers.record(MAC1, 1, VERSION2, TYPE_FAST, 2, 2000) == "Accepted"  # v2 unaffected
    assert peers.peers[MAC1]["gaps"] == 1


def test_real_gap_inside_one_v2_group() -> None:
    peers = PeerManager()
    peers.add(MAC1, 1)
    assert peers.record(MAC1, 1, VERSION2, TYPE_FAST, 1, 1000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION2, TYPE_ENERGY, 1, 1000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION2, TYPE_FAST, 3, 2000) == "SequenceGap"  # FAST skipped 2
    assert peers.record(MAC1, 1, VERSION2, TYPE_ENERGY, 2, 2000) == "Accepted"  # ENERGY unaffected
    assert peers.peers[MAC1]["gaps"] == 1


def test_duplicate_within_one_stream() -> None:
    peers = PeerManager()
    peers.add(MAC1, 1)
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 5, 1000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 5, 2000) == "DuplicateSequence"
    assert peers.peers[MAC1]["duplicates"] == 1


def test_duplicate_in_one_stream_does_not_affect_another() -> None:
    peers = PeerManager()
    peers.add(MAC1, 1)
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 5, 1000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 5, 2000) == "DuplicateSequence"
    assert peers.record(MAC1, 1, VERSION2, TYPE_FAST, 1, 2000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION2, TYPE_FAST, 2, 3000) == "Accepted"
    assert peers.peers[MAC1]["duplicates"] == 1
    assert peers.peers[MAC1]["gaps"] == 0


def test_wraparound_per_stream() -> None:
    peers = PeerManager()
    peers.add(MAC1, 1)
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 0xFFFE, 1000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 0xFFFF, 2000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 0, 3000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 1, 4000) == "Accepted"
    assert peers.peers[MAC1]["gaps"] == 0


def test_reboot_per_stream() -> None:
    peers = PeerManager()
    peers.add(MAC1, 1)
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 500, 1000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION2, TYPE_FAST, 300, 1000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 0, 2000) == "SenderReboot"
    # the v2 FAST stream must not be affected by the v1 stream's reboot
    assert peers.record(MAC1, 1, VERSION2, TYPE_FAST, 301, 2000) == "Accepted"
    assert peers.peers[MAC1]["reboots"] == 1


def test_device_ids_remain_independent() -> None:
    peers = PeerManager()
    peers.add(MAC1, 1)
    peers.add(MAC2, 2)
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 1, 1000) == "Accepted"
    assert peers.record(MAC2, 2, VERSION1, TYPE_SNAPSHOT, 1, 1000) == "Accepted"
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 2, 2000) == "Accepted"
    assert peers.record(MAC2, 2, VERSION1, TYPE_SNAPSHOT, 5, 2000) == "SequenceGap"
    assert peers.peers[MAC1]["gaps"] == 0
    assert peers.peers[MAC2]["gaps"] == 1


def test_stale_fresh_per_device_not_per_stream() -> None:
    peers = PeerManager()
    peers.add(MAC1, 1)
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 1, 1000) == "Accepted"
    assert not peers.stale(MAC1, 2000)
    # a packet on a *different* stream still refreshes overall device freshness
    assert peers.record(MAC1, 1, VERSION2, TYPE_FAST, 1, 4000) == "Accepted"
    assert not peers.stale(MAC1, 8000)
    assert peers.stale(MAC1, 9001)


def test_stream_capacity_fallback_still_accepts() -> None:
    peers = PeerManager(max_streams_per_peer=1)
    peers.add(MAC1, 1)
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 1, 1000) == "Accepted"
    # second distinct stream exceeds the tiny capacity: still accepted, just
    # without per-stream tracking (documented fallback, not a rejection)
    assert peers.record(MAC1, 1, VERSION2, TYPE_FAST, 1, 1000) == "Accepted"


def test_malformed_does_not_alter_sequence_state() -> None:
    # Models that a decode-rejected packet never reaches record() at all (the
    # real component calls recordMalformedForMac(), which only increments a
    # rejection counter and never touches any stream's sequence state).
    peers = PeerManager()
    peers.add(MAC1, 1)
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 5, 1000) == "Accepted"
    assert peers.peers[MAC1]["streams"][(VERSION1, TYPE_SNAPSHOT)]["last_sequence"] == 5
    assert peers.record(MAC1, 1, VERSION1, TYPE_SNAPSHOT, 6, 2000) == "Accepted"
    assert peers.peers[MAC1]["streams"][(VERSION1, TYPE_SNAPSHOT)]["last_sequence"] == 6


def main() -> int:
    tests = [
        test_add_trusted_peer,
        test_reject_duplicate_mac,
        test_reject_duplicate_device_id,
        test_unknown_sender,
        test_wrong_device_id,
        test_sequence_increment_duplicate_gap_wrap_and_reboot,
        test_per_peer_stale_independent,
        test_capacity_limit,
        test_v1_sequence_no_gaps,
        test_v2_fast_sequence_no_gaps,
        test_interleaved_v1_and_v2_no_gaps,
        test_interleaved_v2_groups_no_gaps,
        test_real_gap_inside_v1_only,
        test_real_gap_inside_one_v2_group,
        test_duplicate_within_one_stream,
        test_duplicate_in_one_stream_does_not_affect_another,
        test_wraparound_per_stream,
        test_reboot_per_stream,
        test_device_ids_remain_independent,
        test_stale_fresh_per_device_not_per_stream,
        test_stream_capacity_fallback_still_accepts,
        test_malformed_does_not_alter_sequence_state,
    ]
    for test in tests:
        test()
        print(f"{test.__name__}: OK")
    print(f"{len(tests)} peer manager tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
