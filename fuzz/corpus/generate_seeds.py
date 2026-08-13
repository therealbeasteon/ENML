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


# core/oskeys/include/os/keys/persistence.hpp
KEY_REGISTRY_MAGIC = 0x3147524B  # "KRG1" little-endian
KEY_REGISTRY_VERSION = 1
KEY_REGISTRY_HEADER_BYTES = 32


def key_registry_snapshot(record_count: int, total_version_count: int, body: bytes,
                          total_size: int | None = None) -> bytes:
    """One KRG1 snapshot header, followed by whatever body is supplied.

    total_size must equal the real file length or the header is rejected before
    any record is read, so it is computed rather than supplied unless a seed is
    deliberately testing that check.
    """
    size = KEY_REGISTRY_HEADER_BYTES + len(body) if total_size is None else total_size
    header = b"".join((
        struct.pack("<I", KEY_REGISTRY_MAGIC),
        struct.pack("<H", KEY_REGISTRY_VERSION),
        struct.pack("<H", KEY_REGISTRY_HEADER_BYTES),
        struct.pack("<I", size),
        struct.pack("<H", record_count),
        struct.pack("<H", 0),
        struct.pack("<I", total_version_count),
        struct.pack("<I", 0),
        struct.pack("<Q", 0),
    ))
    assert len(header) == KEY_REGISTRY_HEADER_BYTES, len(header)
    return header + body


def key_registry_seeds() -> dict[str, bytes]:
    """Straddle the header checks, which are what a mutator cannot reach alone.

    total_size must match the file length exactly, so an unseeded run never gets
    past the header no matter how long it runs.
    """
    return {
        "empty_registry": key_registry_snapshot(0, 0, b""),
        "record_count_max": key_registry_snapshot(128, 0, b""),
        "record_count_over_max": key_registry_snapshot(129, 0, b""),
        "version_count_max": key_registry_snapshot(0, 128 * 8, b""),
        "version_count_over_max": key_registry_snapshot(0, 128 * 8 + 1, b""),
        # One record claimed but no body: the record read must fail rather than
        # consume whatever follows in memory.
        "record_claimed_absent": key_registry_snapshot(1, 1, b""),
        # Header claims a length the file does not have, in both directions.
        "total_size_too_large": key_registry_snapshot(0, 0, b"", total_size=4096),
        "total_size_too_small": key_registry_snapshot(0, 0, bytes(64), total_size=32),
        # Truncated below the header.
        "short_header": key_registry_snapshot(0, 0, b"")[:16],
    }


# core/osdevice/include/os/device/access.hpp
DEVICE_ACCESS_MAGIC = b"EDA1"
DEVICE_ACCESS_VERSION = 1
DEVICE_ACCESS_HEADER_BYTES = 32
MMIO_GRANT_BYTES = 24


def mmio_grant(base: int, length: int, access: int) -> bytes:
    record = b"".join((
        struct.pack("<Q", base),
        struct.pack("<Q", length),
        struct.pack("<B", access),
        bytes(7),
    ))
    assert len(record) == MMIO_GRANT_BYTES, len(record)
    return record


def device_access(domain: int, dma: int, grants: bytes, grant_count: int,
                  total_size: int | None = None) -> bytes:
    size = DEVICE_ACCESS_HEADER_BYTES + len(grants) if total_size is None else total_size
    header = b"".join((
        DEVICE_ACCESS_MAGIC,
        struct.pack("<H", DEVICE_ACCESS_VERSION),
        struct.pack("<H", DEVICE_ACCESS_HEADER_BYTES),
        struct.pack("<I", size),
        struct.pack("<B", domain),
        struct.pack("<B", dma),
        struct.pack("<H", grant_count),
        struct.pack("<I", 0),
        struct.pack("<I", 0),
        struct.pack("<Q", 0),
    ))
    assert len(header) == DEVICE_ACCESS_HEADER_BYTES, len(header)
    return header + grants


def device_access_seeds() -> dict[str, bytes]:
    """Straddle the rules that decide how much address space a driver reaches.

    total_size must match the record length exactly, so an unseeded run never
    gets past the header. The rules worth surrounding are the isolation claim
    an IOMMU-less platform cannot back, and canonical grant ordering.
    """
    kernel_resident, isolated_user = 1, 2
    dma_none, dma_iommu, dma_unconfined = 1, 2, 3
    read_only, read_write = 1, 2

    window = mmio_grant(0x1000, 0x1000, read_write)
    second = mmio_grant(0x8000, 0x100, read_only)
    adjacent = mmio_grant(0x2000, 0x1000, read_only)
    overlapping = mmio_grant(0x1800, 0x1000, read_only)
    descending = mmio_grant(0x0100, 0x100, read_only)

    return {
        "no_authority": device_access(isolated_user, dma_none, b"", 0),
        "isolated_iommu_one_window": device_access(isolated_user, dma_iommu, window, 1),
        "isolated_iommu_two_windows": device_access(isolated_user, dma_iommu, window + second, 2),
        # Legal and honest: the driver is in the kernel and the device can
        # master the bus. Degraded platforms must stay expressible.
        "kernel_resident_unconfined": device_access(kernel_resident, dma_unconfined, window, 1),
        # The claim the platform cannot back.
        "isolated_unconfined": device_access(isolated_user, dma_unconfined, window, 1),
        # Canonical ordering, from both directions.
        "adjacent_windows": device_access(kernel_resident, dma_none, window + adjacent, 2),
        "overlapping_windows": device_access(kernel_resident, dma_none, window + overlapping, 2),
        "descending_windows": device_access(kernel_resident, dma_none, window + descending, 2),
        # Windows that describe nothing.
        "empty_window": device_access(kernel_resident, dma_none, mmio_grant(0x1000, 0, read_only), 1),
        "wrapping_window": device_access(
            kernel_resident, dma_none, mmio_grant(0xFFFFFFFFFFFFFFFF, 2, read_only), 1),
        "window_at_top": device_access(
            kernel_resident, dma_none, mmio_grant(0xFFFFFFFFFFFFF000, 0x1000, read_only), 1),
        # Counts and lengths that disagree with the body.
        "grant_count_over_max": device_access(kernel_resident, dma_none, window, 5),
        "grant_count_understates": device_access(kernel_resident, dma_none, window + second, 1),
        "total_size_mismatch": device_access(kernel_resident, dma_none, window, 1, total_size=4096),
        "short_header": device_access(kernel_resident, dma_none, b"", 0)[:16],
        "domain_zero": device_access(0, dma_none, b"", 0),
        "dma_zero": device_access(kernel_resident, 0, b"", 0),
        "access_zero": device_access(kernel_resident, dma_none, mmio_grant(0x1000, 0x1000, 0), 1),
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
    written += write_seeds(root, "keys_registry_snapshot_fuzz", key_registry_seeds())
    written += write_seeds(root, "device_access_fuzz", device_access_seeds())

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
