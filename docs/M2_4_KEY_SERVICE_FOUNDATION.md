# M2.4 Key Service Foundation

M2.4 begins the cryptographic authority layer without pretending that host-test key metadata is already production encryption.

## Separation of identities, authority and secret material

Three things are deliberately different:

```text
KeyId
    public logical locator

KeyObjectHandle
    possession-based operation authority

ProviderKeyReference
    private service/provider locator for secret material
```

A `KeyId` is not a bearer token. Public create/open requests derive the owner from trusted `RequestContext.peer` and use `PrincipalId + UserId` as the ownership key. A caller cannot claim another owner in the request payload.

The provider interface does not return raw long-lived key bytes. It returns an opaque `ProviderKeyReference`. Hardware-backed providers can later map that reference to TPM/TEE/HSM objects without changing public application key ids or IPC layout.

## Bounded registry

`KeyRegistry` contains at most 128 records. Each record contains metadata and an opaque provider reference:

- owner (`PrincipalId + UserId`)
- logical `KeyId`
- version
- purpose
- rights
- destroyed state
- opaque provider reference

Destroyed records remain tombstones. This prevents an old logical id from being silently rebound to a new secret within a registry generation.

Provider generation happens only after a free registry slot and all metadata invariants have been checked. Provider failure therefore cannot publish a partial record.

## Service boundary

M2.4 reserves:

```text
0x0000F030  Key Service
0x0000F031  Key object endpoints
```

Main operations currently implemented:

```text
CreateApplicationDataKey
Open(KeyId)
```

The main endpoint validates trusted caller identity for every request. Successful create/open returns one typed local object endpoint plus bounded descriptor metadata.

The object endpoint is a bearer capability. Its current first management operation is `Destroy`.

Destroying a key:

1. checks server-held destroy rights;
2. asks the provider to destroy its opaque secret object;
3. marks the registry record destroyed/tombstoned;
4. replies to the destroying caller;
5. closes every already-minted object endpoint for the same logical key.

Thus a duplicate key handle cannot remain usable after key-wide destruction.

## Adversarial identity property

The integration test deliberately forks after the owner has a Key Service connection. The child inherits the same main transport descriptor and knows the owner's public KeyId.

`SCM_CREDENTIALS` identifies the actual packet sender. The test resolver maps the child to another `PrincipalId + UserId`, so `Open(KeyId)` returns `access_denied`. Possessing the service transport plus knowing a KeyId is insufficient to borrow another principal's key authority.

## What this milestone does not claim yet

No encryption algorithm is implemented by this foundation slice. There is no production provider and no `system.keys` executable yet. Therefore this code does **not** claim encrypted private storage, hardware-backed keys, persistence, rotation, recovery or verified-boot sealing.

Those come only after the authority/lifecycle contracts are stable and the cryptographic profile/provider implementation can be reviewed independently.

## Next implementation slice

The next Key Service slice should add:

- a collision-resistant production `KeyIdSource`;
- a software-backed CI provider behind the same non-exporting provider API;
- an explicit versioned AEAD ciphertext envelope;
- encrypt/decrypt provider operations using a reviewed current cryptographic profile;
- persistence for logical key metadata and provider-wrapped material;
- rotation semantics that keep old versions available while existing ciphertext still references them;
- service restart tests;
- explicit cryptographic deletion behavior.

Hardware-backed providers, early-boot sealing and attestation remain separate BSP/security integration work.
