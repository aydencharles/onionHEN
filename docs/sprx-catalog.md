# SPRX Catalog Manifest

`SprxCatalog` uses a deliberately small INI manifest. This follows the
project's existing `config.ini` conventions and the reference `ploader.ini`
without introducing another parser or a nested JSON schema.

```ini
[plugin.base-runtime]
path=/data/OnionHEN/sprx/base-runtime.sprx
exact_title_ids=CUSA12345,CUSA67890
priority=10

[plugin.overlay]
path=/data/OnionHEN/sprx/overlay.sprx
exact_title_ids=CUSA12345
auto_start=true
priority=100
dependencies=base-runtime

[plugin.native]
path=/data/OnionHEN/sprx/native.prx
title_id_prefixes=PPSA
auto_start=true
```

Each plugin has one `[plugin.<id>]` section. The ID is unique and uses the
same printable identifier rule as the existing plugin UI and plugin manager.
`path` is an absolute `.sprx` or `.prx` path; the loader performs the final
`realpath()` and regular-file checks before touching the target process.

`exact_title_ids` and `title_id_prefixes` are comma-separated allowlists. A
plugin is eligible for a target only when one of these fields matches its
Title ID. `auto_start=true` selects the roots for automatic startup. A
dependency is included in the startup plan even if it is not auto-started
itself. Dependencies must be declared and must also match the target Title
ID; missing, mismatched, duplicate, or cyclic dependencies reject the plan.

Startup order is a topological order. Among entries whose dependencies are
already satisfied, larger `priority` values start first; ties are resolved by
plugin ID for deterministic behavior. Dependencies always precede their
dependents, regardless of priority.

This is intentionally not a general package-management format. Version
ranges, optional dependencies, environment expressions, permissions, and
per-plugin retry overrides are deferred until a concrete use case requires
them. Keeping those concerns out of Phase 2 makes malformed manifests easy to
reject and keeps policy in `SprxLoader` rather than duplicating it here.
