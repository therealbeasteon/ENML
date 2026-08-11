# M4.10j — Rollback-safe profile protector restore

Persistent protector records are untrusted disk input. Before a provider-owned wrapped profile root can be restored, Cookie compares the record against independently trusted current state: UserId, measured boot digest, rollback-resistant hardware credential-gate slot, exact monotonic security epoch, and minimum protector generation. Normal profile unlock policy must also succeed, including credential acceptance and the absence of destruction-pending state.

The security epoch comparison is exact. A lower record is rollback; a higher record is impossible untrusted future state. Neither can authorize hardware state changes.

This slice does not restore raw root bytes in the boot layer. It only decides whether an opaque provider restore may proceed.
