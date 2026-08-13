# M4.10o — Generation-bound encrypted Storage

## Threat

M4.10m binds the persistent wrapped object-key record to an object-key generation, and M4.10n defines crash-consistent generation publication. The encrypted chunk format also has to carry that generation. Otherwise an attacker with an older valid ciphertext record for the same UserId, object ID and chunk index could replay stale plaintext while leaving newer key/namespace metadata intact.

## Mechanism

Protected chunk format v2 authenticates `object_generation` together with the crypto profile, UserId, stable object ID, chunk index, plaintext length and reserved flags.

`ProtectedChunkCrypto::open` never treats the generation in the on-disk record as authority. The caller supplies the expected generation from trusted Storage namespace state. User, object ID, generation and chunk index must all match before provider AEAD open is attempted.

## Atomic replacement invariant

A new generation is complete only when all of the following refer to the same non-zero generation:

1. the provider-bound persistent object-key record;
2. every protected ciphertext chunk;
3. the durable commit record;
4. the trusted namespace entry.

Recovery must never select a generation by filename age or directory enumeration. It follows the authenticated commit/namespace state. A generation mismatch fails closed.

## Compatibility

No production Cookie encrypted Storage format has shipped. M4.10o therefore advances the protected chunk format from v1 to v2 rather than carrying a known replay weakness forward. Old v1 records are rejected by the v2 decoder.
