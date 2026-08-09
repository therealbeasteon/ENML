# M2.6 — Durable provider-wrapped key persistence

M2.6 makes logical Key Service state survive a service/process restart without placing raw long-lived key bytes in the ENML registry or public IPC surface.

## Trust split

`KeyRegistry` owns logical metadata and authorization state:

- `KeyId`
- trusted `KeyOwner { PrincipalId, UserId }`
- purpose and rights
- current key version
- bounded retained historical versions

`PersistentKeyProvider` owns durable secret representation. ENML core code receives only an opaque provider blob. A production provider may use a TPM/TEE/HSM sealed object or durable secure-object locator. The OpenSSL implementation is CI-only and uses a fixed test wrapping key; it is not a production root of trust.

Each provider blob is authenticated against a canonical `KBD1` binding containing the logical KeyId, full 128-bit PrincipalId, full 64-bit UserId, purpose, rights, and the specific key version. A valid provider blob therefore cannot be silently transplanted into another logical key/owner/version record.

## Registry snapshot

The durable registry file is `key-registry-v1.bin` with magic `KRG1` and explicit little-endian fields. Native C++ object layout is never serialized.

The snapshot is bounded to 288 KiB. It contains at most 128 logical records and at most eight retained versions per live logical key. Every live version stores an opaque provider blob of at most 256 bytes. Destroyed KeyIds remain tombstones so restart cannot make an old identifier reusable.

The snapshot deliberately stores no process-local `ProviderKeyReference`; restore creates fresh ephemeral provider references in the new provider instance.

## Publication transaction

Mutating operations publish a candidate registry with:

```text
unlink stale .key-registry-v1.tmp
        ↓
openat(O_CREAT|O_EXCL|O_NOFOLLOW, 0600)
        ↓
write explicit KRG1 candidate
        ↓
fsync(temp)
        ↓
renameat(temp → key-registry-v1.bin)
        ↓
fsync(state directory)
        ↓
publish candidate in memory
```

The caller supplies an already-authorized directory handle; the persistence layer does not accept an application-controlled absolute state path. The temporary filename is never followed as a symlink.

Create and rotate generate provider state on a candidate registry first. If publication fails before replacement, the newly generated provider object is destroyed and the live in-memory registry is unchanged. If replacement already happened but the directory fsync subsequently reports failure, the candidate is retained in memory and the operation reports the durability error instead of destroying provider state referenced by the visible replacement snapshot.

Destroy is ordered differently for safety: the durable tombstone is published first, then provider objects are physically destroyed. A late provider cleanup failure cannot make the logical key live again.

## Rotation and restart invariant

For a logical key rotated from v1 to v2:

```text
KeyId K
  current = v2
  retained provider objects = { v1, v2 }

new encryption → v2 only
old v1 ciphertext → decryptable
v2 ciphertext → decryptable
```

After Key Service restart, a fresh provider restores both retained versions from their provider-owned blobs. The logical descriptor still reports v2, old ciphertext remains decryptable, and the next rotation becomes v3.

## Verification gates

The M2.6 tests cover:

- opaque provider persist/restore across provider restart
- wrong binding, tamper, and truncation rejection
- full 64-bit UserId persistence
- registry create → rotate → restart → historical decrypt
- durable destroyed-KeyId tombstones and no KeyId reuse
- temporary-name symlink non-following
- snapshot file mode 0600
- a real cross-process Key Service restart, reopen of the same KeyId, v1/v2 decrypt after restart, and subsequent v3 rotation

The same key gates run with GCC, Clang, and native AArch64.

## Explicitly not solved in M2.6

M2.6 does not claim filesystem rollback resistance or a production hardware root. A privileged attacker who can replace an older otherwise-valid registry snapshot is outside this slice. Binding durable key state to measured boot / hardware monotonic security state remains a later root-key and rollback-protection milestone.
