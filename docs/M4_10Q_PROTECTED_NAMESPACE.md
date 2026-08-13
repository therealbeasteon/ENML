# M4.10q — Trusted protected namespace registry

Encrypted object identity must survive rename and reboot without being derived from a pathname. This slice adds a bounded registry keyed by trusted principal + user + confined relative path and stores a stable random `ProtectedObjectId` plus the currently published generation.

The path is only namespace lookup metadata. Cryptographic records authenticate the stable object ID and generation.

Replacement uses a two-step generation API: `propose_next_generation()` computes N+1 without changing authoritative state; `publish_generation()` changes the registry only after the durable publication protocol has reached namespace publication. A failed staging write therefore leaves N authoritative.

The registry rejects duplicate path entries, stale generation expectations, invalid identities and capacity exhaustion. The next slice must make this registry itself durable and authenticated across reboot before system.storage switches normal `atomic_replace` to protected publication.
