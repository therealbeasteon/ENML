# M4.10n — Crash-consistent protected Storage publication

Encrypted Storage publication is a multi-step durable transaction. Cookie must never publish a namespace entry that refers to ciphertext whose object key is not durably recoverable, and must never destroy the old generation before the new generation is durably committed.

The required ordering is:

1. create/generate the new provider-owned object key and make its wrapped reference durable;
2. encrypt the new object/chunks and make the ciphertext durable;
3. write and durably commit a small authenticated publication record binding the target object identity and generation to the new durable key/ciphertext generation;
4. atomically publish the namespace pointer/name to the committed generation;
5. only after namespace publication is durable may the previous generation and its key be retired.

A crash before the commit record is durable may discard the incomplete new staging generation. A crash after the commit record is durable requires explicit recovery; the service must finish or roll back using authenticated durable state and may not silently expose an older generation. Destruction-pending profile state overrides all normal recovery and resumes cryptographic erasure instead of publishing data.

This slice defines and tests the phase ordering. The following slice must connect the phases to the concrete system.storage atomic_replace path and its durable object/key metadata backend. The Linux host backend remains only a development substrate while this protocol is kept platform-neutral for Cookie Kernel storage drivers.
