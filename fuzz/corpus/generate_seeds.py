#!/usr/bin/env python3
"""Materialize libFuzzer seed corpora for the ENML fuzz targets.

Seeds are generated rather than checked in as binary blobs. The structure of
every seed is then reviewable as source, which matters more here than for a
typical corpus: these bytes encode ENML's frozen wire contracts, and an opaque
blob in the tree cannot be audited against them.

Why seeds matter at all: a coverage-guided fuzzer starting from random bytes
spends effectively its entire budget failing the first gate. `OSIP` magic is a
1-in-2^32 guess, so an unseeded ipc_decoder run never reaches flag validation,
payload bounds or UTF-8 handling. Seeding past the front door is the difference
between fuzzing the parser and fuzzing the magic check.

Usage:
    generate_seeds.py <output-root>

Creates <output-root>/<target>/ populated with seed files.
"""

from __future__ import annotations

import pathlib
import struct
import sys

# Wire header field widths come from core/osipc/include/os/ipc/constants.hpp and
# wire.hpp. Keep these in step with WireHeaderV1 if the transport revises.
WIRE_MAGIC = b"OSIP"
WIRE_HEADER_SIZE = 40
TRANSPORT_VERSION_V1 = 1

FLAG_REQUEST = 1 << 0
FLAG_RESPONSE = 1 << 1
FLAG_EVENT = 1 << 2
FLAG_ERROR = 1 << 3
FLAG_ONEWAY = 1 << 4
FLAG_CANCELLABLE = 1 << 5

ECHO_SERVICE_ID = 0x0000F001

# core/ospkg/include/os/package/analyzer.hpp
PACKAGE_MANIFEST_MAGIC_V1 = 0x314B5045  # "EPK1" little-endian
PACKAGE_MANIFEST_VERSION_V1 = 1


def wire_header(
    flags: int,
    *,
    service_id: int = ECHO_SERVICE_ID,
    operation_id: int = 1,
    request_id: int = 1,
    payload_size: int = 0,
    handle_count: int = 0,
) -> bytes:
    """Build one canonical little-endian WireHeaderV1."""
    header = b"".join(
        (
            WIRE_MAGIC,
            struct.pack("<H", WIRE_HEADER_SIZE),
            struct.pack("<H", TRANSPORT_VERSION_V1),
            struct.pack("<I", flags),
            struct.pack("<I", service_id),
            struct.pack("<I", operation_id),
            struct.pack("<Q", request_id),
            struct.pack("<I", payload_size),
            struct.pack("<H", handle_count),
            struct.pack("<H", 0),  # reserved, must be zero
            struct.pack("<I", 0),  # checksum, reserved as zero in v1
        )
    )
    assert len(header) == WIRE_HEADER_SIZE, len(header)
    return header


def rpc_error_envelope(domain: int, code: int) -> bytes:
    """The fixed 8-byte ErrorDomain/reserved/code envelope."""
    return struct.pack("<HHI", domain, 0, code)


def ipc_decoder_seeds() -> dict[str, bytes]:
    """Cover each valid primary message class and the modifier combinations.

    Reaching flag validation at all requires a well-formed magic, header size,
    version and zeroed reserved field, so every seed supplies those and varies
    only what the decoder is meant to discriminate.
    """
    error_payload = rpc_error_envelope(2, 18)  # ipc domain, protocol_violation
    return {
        "request_empty": wire_header(FLAG_REQUEST),
        "request_payload": wire_header(FLAG_REQUEST, payload_size=5) + b"hello",
        "request_oneway": wire_header(FLAG_REQUEST | FLAG_ONEWAY),
        "request_cancellable": wire_header(FLAG_REQUEST | FLAG_CANCELLABLE),
        "response_empty": wire_header(FLAG_RESPONSE),
        "response_error": (
            wire_header(FLAG_RESPONSE | FLAG_ERROR, payload_size=len(error_payload))
            + error_payload
        ),
        "event_empty": wire_header(FLAG_EVENT, operation_id=2),
        # A header whose declared payload_size exceeds the bytes present. This
        # seeds the truncation path directly rather than waiting for the fuzzer
        # to discover the length/content mismatch by chance.
        "short_payload": wire_header(FLAG_REQUEST, payload_size=64) + b"\x00" * 8,
        # Multi-byte UTF-8 in the payload gives the validator something with
        # continuation bytes to mutate into overlongs and surrogates.
        "utf8_payload": (
            wire_header(FLAG_REQUEST, payload_size=13)
            + struct.pack("<I", 9)
            + "héllo✓".encode("utf-8")[:9]
        ),
    }


def rpc_error_seeds() -> dict[str, bytes]:
    """One valid envelope per ErrorDomain, plus the boundary cases."""
    seeds = {
        f"domain_{domain}": rpc_error_envelope(domain, 1)
        for domain in range(1, 7)  # core..security
    }
    seeds["ipc_protocol_violation"] = rpc_error_envelope(2, 18)
    # code == 0 and an out-of-range domain are both rejected; seeding them puts
    # the fuzzer next to the accept/reject boundary instead of far from it.
    seeds["zero_code"] = rpc_error_envelope(2, 0)
    seeds["domain_out_of_range"] = rpc_error_envelope(7, 1)
    return seeds


def package_manifest_seeds() -> dict[str, bytes]:
    """Get past magic and version so the field parser is reachable.

    These are deliberately not complete valid manifests. The analyzer's later
    fields are what we want the fuzzer to explore; hand-writing a fully valid
    manifest here would freeze a second copy of that layout in Python and rot.
    """
    header = struct.pack("<IH", PACKAGE_MANIFEST_MAGIC_V1, PACKAGE_MANIFEST_VERSION_V1)
    return {
        "magic_version": header,
        "magic_version_padded": header + b"\x00" * 32,
        "wrong_version": struct.pack("<IH", PACKAGE_MANIFEST_MAGIC_V1, 2),
    }


def storage_relative_path_seeds() -> dict[str, bytes]:
    """Straddle the accept/reject boundary of the storage confinement parser.

    Unlike the binary targets there is no magic to clear here, so the seeds are
    chosen to sit next to each rejection rule rather than to get past a gate:
    a mutator that starts from "a/../b" reaches the traversal check immediately.
    """
    # core/osstorage/include/os/storage/path.hpp
    max_segment_bytes = 255
    accepted = {
        "single": "a",
        "file": "file.txt",
        "nested": "dir/file.txt",
        "deep": "a/b/c/d/e",
        "dotted_name": "archive.tar.gz",
        "utf8_name": "документы/файл.txt",
        "max_segment": "x" * max_segment_bytes,
    }
    rejected = {
        "absolute": "/etc/passwd",
        "parent": "..",
        "current": ".",
        "traversal": "a/../b",
        "current_segment": "a/./b",
        "empty_segment": "a//b",
        "trailing_slash": "a/",
        "leading_slash": "/a",
        "backslash": "a\\b",
        "over_max_segment": "x" * (max_segment_bytes + 1),
    }

    seeds = {name: value.encode("utf-8") for name, value in {**accepted, **rejected}.items()}
    # Byte sequences that are not valid UTF-8 at all, plus an embedded NUL.
    # These cannot be expressed as Python str, so they are added directly.
    seeds["nul_embedded"] = b"a\x00b"
    seeds["utf8_overlong"] = b"a/\xc0\xafb"
    seeds["utf8_surrogate"] = b"a/\xed\xa0\x80"
    seeds["utf8_truncated"] = b"a/\xe2\x82"
    return seeds


# core/osboot/include/os/boot/state.hpp
BOOT_STATE_MAGIC = b"EBS1"
BOOT_STATE_HEADER_BYTES = 32
BOOT_STATE_VERSION = 1
BOOT_STAGE_RECORD_BYTES = 40
SHA256_DIGEST_BYTES = 32


def boot_stage(kind: int, seed: int) -> bytes:
    """One 40-byte stage measurement record."""
    digest = bytes((seed + index) % 256 for index in range(SHA256_DIGEST_BYTES))
    record = struct.pack("<HBBI", kind, 1, 0, 0) + digest
    assert len(record) == BOOT_STAGE_RECORD_BYTES, len(record)
    return record


def boot_state(lifecycle: int, verification: int, security_version: int, stages: bytes,
               stage_count: int, capabilities: int = 0) -> bytes:
    header = b"".join((
        BOOT_STATE_MAGIC,
        struct.pack("<H", BOOT_STATE_HEADER_BYTES),
        struct.pack("<H", BOOT_STATE_VERSION),
        struct.pack("<BB", lifecycle, verification),
        struct.pack("<H", 0),
        struct.pack("<Q", security_version),
        struct.pack("<H", stage_count),
        struct.pack("<H", 0),
        struct.pack("<I", capabilities),
        struct.pack("<I", 0),
    ))
    assert len(header) == BOOT_STATE_HEADER_BYTES, len(header)
    return header + stages


def boot_state_seeds() -> dict[str, bytes]:
    """Records that clear magic/version so the coherence rules are reachable.

    The interesting behaviour is the accept/reject boundary between a complete
    closed chain and every near-miss, so the seeds sit on both sides of it.
    """
    # immutable_first_stage | monotonic_counter
    backed = 0x01 | 0x10
    full_kinds = [1, 2, 3, 4, 5]
    full = b"".join(boot_stage(kind, kind * 40) for kind in full_kinds)
    kernel_only = boot_stage(3, 90)

    return {
        # Accepted.
        "closed_verified_full": boot_state(2, 2, 7, full, len(full_kinds), backed),
        "open_verified_kernel_only": boot_state(1, 2, 1, kernel_only, 1, 0x10),
        "closed_failed_full": boot_state(2, 1, 7, full, len(full_kinds), backed),
        "bare_platform_verified": boot_state(1, 2, 0, kernel_only, 1, 0),
        "open_failed_empty": boot_state(1, 1, 0, b"", 0),
        # Rejected, and each for a different reason.
        "closed_verified_partial": boot_state(2, 2, 7, kernel_only, 1),
        "verified_no_stages": boot_state(1, 2, 0, b"", 0),
        "lifecycle_zero": boot_state(0, 2, 0, kernel_only, 1),
        "verification_zero": boot_state(1, 0, 0, kernel_only, 1),
        "stage_count_over_max": boot_state(1, 1, 0, full, 9),
        "duplicate_stage": boot_state(1, 1, 0, boot_stage(3, 1) + boot_stage(3, 2), 2),
        "unknown_stage_kind": boot_state(1, 1, 0, boot_stage(6, 1), 1),
        "unknown_capability": boot_state(1, 1, 0, kernel_only, 1, 0x80000000),
        "closed_verified_no_root": boot_state(2, 2, 0, full, len(full_kinds), 0x04),
        "rollback_claim_unbacked": boot_state(1, 1, 5, kernel_only, 1, 0x01),
        "trailing_byte": boot_state(1, 1, 0, kernel_only, 1) + bytes([0]),
    }


def write_seeds(root: pathlib.Path, target: str, seeds: dict[str, bytes]) -> int:
    directory = root / target
    directory.mkdir(parents=True, exist_ok=True)
    for name, payload in seeds.items():
        (directory / name).write_bytes(payload)
    return len(seeds)


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(f"usage: {argv[0]} <output-root>", file=sys.stderr)
        return 1

    root = pathlib.Path(argv[1])
    written = 0
    written += write_seeds(root, "ipc_decoder_fuzz", ipc_decoder_seeds())
    written += write_seeds(root, "rpc_error_fuzz", rpc_error_seeds())
    written += write_seeds(root, "package_manifest_fuzz", package_manifest_seeds())
    written += write_seeds(
        root, "storage_relative_path_fuzz", storage_relative_path_seeds())
    written += write_seeds(root, "boot_state_fuzz", boot_state_seeds())

    # osidlc seeds are the checked-in interface definitions themselves. They are
    # the only guaranteed-valid OSIDL in existence, so nothing synthetic here
    # would be a better starting point.
    interfaces = pathlib.Path(__file__).resolve().parents[2] / "interfaces"
    osidl_dir = root / "osidlc_compiler_fuzz"
    osidl_dir.mkdir(parents=True, exist_ok=True)
    for source in sorted(interfaces.rglob("*.osidl")):
        (osidl_dir / source.name).write_bytes(source.read_bytes())
        written += 1

    print(f"wrote {written} seed files under {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
