# M4.10r security invariant

A namespace snapshot is authoritative only when both conditions hold: its AEAD authentication succeeds under the profile metadata key, and its authenticated freshness tuple `{UserId, SecurityEpoch, sequence}` is accepted against independently trusted rollback-resistant state. Either condition failing leaves the registry unchanged.