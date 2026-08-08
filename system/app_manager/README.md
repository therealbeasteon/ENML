# ENML App Manager

M1.3 implements the first trusted application-launch vertical slice.

The public launch decision is package-based, not path-based. `ApplicationManager::launch()` accepts a canonical `PackageId` and trusted `UserId`, resolves the signer-bound application and active immutable generation from the package registry, selects an internal launch target registered by Package Service, and starts the already-opened executable descriptor under the existing sandbox baseline.

Applications cannot choose their Linux executable path, native credentials, `PrincipalId`, package generation, content digest, or sandbox policy. Those are trusted system inputs.

See `docs/M1_3_APP_MANAGER.md` for the milestone invariants and limits.
