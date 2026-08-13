# M4.10p — Protected atomic replace coordinator

This slice converts the M4.10n publication ordering into an executable coordinator shared by future host and Cookie-kernel Storage backends.

For one stable protected object, a replacement from generation N to N+1 is legal only when user and object identity are unchanged and the generation strictly advances. The coordinator requires these backend operations in order:

1. persist the wrapped key for N+1;
2. persist authenticated ciphertext for N+1;
3. persist the commit record linking N to N+1;
4. publish the trusted namespace entry for N+1;
5. retire N.

Failure at any step returns immediately. In particular, the previous generation cannot be retired before namespace publication succeeds. Failure-injection tests exercise every phase.

The coordinator intentionally does not derive object identity from a pathname and does not invent filesystem durability semantics. The next slice must add a trusted persistent namespace/object registry that maps a confined Storage object to its stable `ProtectedObjectId` and current generation, then adapt `system.storage::atomic_replace` to that registry and this coordinator.
