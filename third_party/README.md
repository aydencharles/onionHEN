# Third-party dependencies

Everything in this directory is maintained outside OnionHEN. Keeping external
code here makes `source/` exclusively first-party code.

| Path | Form | Role in OnionHEN |
|------|------|------------------|
| [`7zip_sdk/`](7zip_sdk/) | Vendored source | LZMA support used by the unpacker |
| [`cjson/`](cjson/) | Vendored source | JSON parsing and serialization |
| [`cheat_support/`](cheat_support/) | Vendored source | AES, base64, miniz and SHA-256 used by cheat parsers |
| [`keystone/`](keystone/) | Headers + prebuilt archive | ShnExt assembly support |
| [`kstuff-lite/`](kstuff-lite/) | Git submodule | Produces the optional embedded `kstuff.elf` |

Third-party file names retain their upstream spelling even when it differs
from the project's snake_case convention. This keeps upstream updates easy to
review.

Source-built or downloaded fallback dependency blobs are cached in
`.cache/dependencies/` and ignored by Git. `scripts/sync_dependencies.sh`
stages the required bootstrapper input from that cache; generated blobs do not
belong in `source/`.

## Runtime-only external dependency

[ps5-payload-dev/elfldr](https://github.com/ps5-payload-dev/elfldr) on port 9021
is supplied by the runtime environment for the initial bootstrap.

```bash
git submodule update --init --recursive
./scripts/sync_dependencies.sh
```
