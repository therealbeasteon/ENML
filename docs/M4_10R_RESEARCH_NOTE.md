# M4.10r research conclusions

Current Android FBE guidance separates credential-encrypted file data from metadata encryption and protects metadata keys under a boot-trusted hardware key service. Cookie adopts the separation-of-domains and boot-bound-key principles, not Android's Linux/fscrypt implementation.

Apple Secure Enclave documentation describes anti-replay state for security-critical protected memory and secure non-volatile state. Cookie adopts the requirement that integrity authentication alone is insufficient against replay and binds persistent namespace acceptance to independent monotonic freshness evidence.

Cookie therefore requires two properties before namespace metadata can become authoritative after boot: cryptographic authentication under a profile-scoped metadata key and equality/freshness against independent rollback-resistant state.