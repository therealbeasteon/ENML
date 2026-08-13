# M4.10r status

Implemented:
- dedicated profile Storage metadata key purpose;
- provider scope enforcement for that purpose;
- rollback-bound namespace snapshot header;
- independent user/epoch/sequence freshness validation;
- negative freshness tests in the Storage CI matrix.

Remaining for M4.10s:
- canonical entry serialization;
- AEAD sealing/opening of complete snapshots;
- duplicate/capacity/path/object validation on restore;
- all-or-nothing registry replacement after authentication and freshness checks.