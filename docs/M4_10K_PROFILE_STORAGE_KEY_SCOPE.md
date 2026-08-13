# M4.10k — Profile Storage key scope

Cookie private Storage needs a cryptographic authority beneath the profile root that is distinct from application Key Service authority.

## Hierarchy

`profile root -> profile_storage_aead object/data keys`

Application data keys remain below application roots:

`profile root -> application root -> application_data_aead keys`

The two key purposes are not interchangeable. `KeyHierarchy` and the provider boundary both reject purpose/scope confusion.

## Why this matters

A single profile-root destruction must cut off every Storage key for that user while preserving the architectural rule that applications never receive profile-wide Storage authority. The Storage Service will own object-key lifecycle and use the provider for AES-256-GCM; applications receive only Storage capabilities and plaintext through authorized service operations.

This slice does not yet encrypt persistent Storage bytes. It creates the cryptographic purpose boundary required for the next AEAD integration.

## Reference guidance

NIST SP 800-38F reinforces separating bulk-data encryption keys from key-wrapping/protector roles. BitLocker and modern mobile FBE systems similarly use envelope hierarchies rather than deriving disk/data keys directly from a user password. Cookie imports those properties, not their vendor ABI.
