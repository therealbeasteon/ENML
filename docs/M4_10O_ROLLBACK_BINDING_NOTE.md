# M4.10o — Encrypted Storage generation binding

Protected chunk v2 authenticates the object generation together with user, stable object identity and chunk index. Restore/open must compare that authenticated generation with independently trusted Storage namespace state. An older valid ciphertext record for the same object/chunk therefore cannot be replayed after generation advancement.
