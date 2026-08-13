# M4.10s plan — encrypted namespace snapshot codec and restore

This slice will make the protected namespace restart-persistent without trusting raw filesystem metadata.

The codec will use a canonical binary representation with explicit bounds. Snapshot authentication will cover header and entries. Recovery will parse into temporary bounded state, verify every entry, reject duplicate principal/user/path tuples and duplicate stable object identities within a profile, then validate monotonic freshness before replacing the live registry.

No partial registry mutation is allowed during parse or authentication. A malformed, stale or tampered snapshot leaves the existing/empty trusted registry unchanged.

The next service-integration slice may redirect public `atomic_replace` only after this restart recovery path is tested.