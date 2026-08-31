# OnionHEN DPI pkg-server API

Two servers run in the same process (`third_party/pkgserver/pkg_server.c`):

| Port | What | Kind |
|------|------|------|
| **9090** | pkgs install / chunk upload API | JSON (transfer, preflight, control) |
| **12800** | web UI (single-file bundle) + status/progress | HTTP + SSE |

Everything that was polled by the UI now comes over one SSE stream; there is
no `/api/status`, `/progress`, `/status`, `/staged-bytes`, `/api/upload` or
`/api/install` anymore.

---

## 1) DPI web UI — TCP 12800

### Static routes

| Route | Description |
|-------|-------------|
| `GET /` | serves the built single-file web app (index.html) |
| `GET /index.html` | same bundle |
| anything else | `404` (see [error shapes](#error-shapes)) |

### `GET /api/stream` — unified status + progress (SSE)

One persistent `text/event-stream` connection. Every ~250 ms the server writes
one `data:` line with a full JSON frame terminated by a blank line. `bytes` is
appended to the JSON so a browser can show progress without any polling.

Example frame:

```json
data: {"ok":true,"ip":"192.168.1.10","staged":true,"language":"es","phase":"uploading","error":"","busy":false,"name":"MyGame.pkg","total":4194304000,"cid":"UP0001-XXXX-XXXXX_00-XXXXXXXXXXXXXXXXXX","bytes":123456789}

```

| Field | Type | Meaning |
|-------|------|---------|
| `ok` | bool | always `true` while the server is up |
| `ip` | string | host address reported by the console |
| `staged` | bool | a complete staged pkg currently exists |
| `language` | string | Web UI locale as a BCP-47 tag: `zh-Hans`, `zh-Hant`, `en`, `ja`, `ko`, `fr`, `de`, `it`, `es`, `pt-BR`, `pl`, `ru`, `ar` or `th`. Resolved exactly like the Toolbox language: `toolbox.language` when set, else the console's system language. Re-synced on every server (re)start, on settings reload, and live on console-language switches (the daemon polls SCE language every ~5 s and pushes `BREW_UTIL_SET_SYSTEM_LANG` to util); `en` is the cold-start fallback |
| `phase` | string | see phases below |
| `error` | string | hex Sony error code when `phase:"error"`, else `""` |
| `busy` | bool | another install operation is holding the mutex |
| `name` | string | file name of the current session (from the `offset=0` opener) |
| `total` | number | expected total bytes of that session |
| `cid` | string | content id of the last install (empty until one is known) |
| `bytes` | number | bytes received this session (reset on each `offset=0` opener) |

`name`/`total`/`bytes` are per-session/per-file, so a client can attribute
progress to the file it is actually uploading (e.g. ignore a frame whose
`name` does not match).

The web UI derives transfer speed and an **ETA** (estimated remaining time)
entirely client-side from `bytes`/`total`: a rolling ~5 s rate window
smooths the bursty 128 MB block completions, and the displayed ETA is
quantized (5 s / 30 s / 1 min steps) so it does not flicker. The server
never sends speed or ETA values.

#### Phases

Base phases from the server state machine:

| phase | meaning |
|-------|---------|
| `idle` | no upload, not installing |
| `uploading` | chunked upload in progress |
| `staged` | full pkg staged, ready to finalize |
| `installing` | Sony install running |
| `complete` | install finished OK |
| `error` | install failed (see `error`) |

When a Sony task exists, the live phase string from
`sceAppInstUtilGetInstallStatus` is mirrored instead (e.g. `transferring`,
`validating`, `installing`, `playable`, `failed`).

**Terminal auto-reset:** `complete`/`error` are held for `WEBUI_TERM_RESET_MS`
(5000 ms) so the browser's install-wait can observe them, then the server snaps
back to `idle` and clears `cid`, `staged`, `name` and `total`. The stream never
pins `phase:"complete"` + `staged:true` forever.

### File-kind detection (browser side, no endpoint)

The web app never asks the server to classify a package: it inspects the
dropped `.pkg` locally, reading only small slices of the file:

- **PS4** — bare `\x7FCNT` container at offset 0. Reads `param.sfo` (entry
  `0x1000`) → `CATEGORY`: `GD` base, `GP`/`PA` update, `AC`/`AP` add-on;
  content id is scanned from the container.
- **PS5** — `\x7FFIH` finalized image → embedded `\x7FCNT` metadata container
  at `FIH+0x58` → `param.json` (entry `0x2000`) yields `contentId`, `titleId`
  and `titleName` (under `localizedParameters["<locale>"]`, skipping
  `defaultLanguage`). Entry-table and payload offsets inside the PS5 container
  are **relative to the container base**, not the file. Retail PS5 metadata
  holds no category, so the kind falls back to a name heuristic (update /
  dlc / add-on markers) and the label renders like `PS5 · Add-on`.

Per-file `platform`/`kind`/`contentId`/`title` never round-trip through the
server; `cid` in the SSE frames remains the last *installed* content id.

---

## 2) Install / chunk API — TCP 9090

Base for all requests: `http://<console>:9090`

### `GET /ping` — liveness + busy probe

```json
{"ok":true,"name":"pkg-server","version":"1.0","fw":7,"port":9090,"busy":false}
```

Used by the internal takeover/redeploy logic (a new instance pings before it
replaces a running one) and as a healthy check. Do not remove.

### `GET /staged-size?name=X` — reuse preflight

Reports the size of the staged `X` (under `/user/data/tmp`).

```json
{"ok":true,"size":4194304000}
```

Errors: `400 {"ok":false,"error":"bad_name"}` on an invalid name,
`404 {"ok":false,"error":"not_found"}` when nothing is staged under `X`.

The browser uses this to skip re-uploading a file that is already staged in
full (`size == total`).

### `POST /install` — upload (chunk or single-shot)

#### Chunk mode (`?offset=` … `?total=`)

Required query params: `name`, `offset`, `total` (`total > 0`). Body is a raw
binary range. Coupled with an optional `X-Pkg-Size` header, the server can
detect an identical already-staged copy and reply with a reuse skip.

**Session opener (`offset=0`)** — resets the session byte counter, records
`name`/`total` for the SSE stream, starts (or reuses) the staged file:

```http
POST /install?name=MyGame.pkg&offset=0&total=4194304000&head256=<sha256 of the first 1MB>
Content-Type: text/plain
X-Pkg-Size: 4194304000

<first 1 MB of the pkg>
```

- Identical staged copy (size + head256 match) →
  `200 {"ok":true,"reuse":true,"detail":"skip to finalize"}`
- Otherwise → `200 {"ok":true,"chunk":true,"offset":0,"written":1048576,"next":1048576}`
  (or `500 {"ok":false,"error":"stage_open_failed"}` if the stage file can't be
  opened; new updates get `_upd2.pkg`, `_upd3.pkg`, …, when the base name is
  taken with a different size).

**Ranged blocks** (any `offset > 0`, up to ~128 MB each, send in parallel):

```http
POST /install?name=MyGame.pkg&offset=1048576&total=4194304000
Content-Type: text/plain

<128 MB block>
```

→ `200 {"ok":true,"chunk":true,"offset":1048576,"written":135266304,"next":135266304}`

A block before the opener fails with `409 {"ok":false,"error":"no_session","detail":"send the offset=0 chunk first"}`.

**Finalize** (empty body, `offset == total`, `finalize=1`):

```http
POST /install?name=MyGame.pkg&offset=4194304000&total=4194304000&finalize=1
```

Runs the install (verify staged size → `sceAppInstUtilInstallByPackage` →
register task). Replies:

- with `wait` unset/0 — async mode:
  ```json
  {"ok":true,"installed":false,"via":"patch","content_id":"UP0001-XXXX-XXXXX_00-XXXXXXXXXXXXXXXXXX","tmp_file":"/user/data/tmp/MyGame.pkg","staged":"kept","phase":"accepted","note":"watch the SSE stream /api/stream for phase=playable"}
  ```
- with `wait=<seconds>` (cap `INSTALL_WAIT_MAX_S`) the server blocks until the
  terminal state and returns `installed:true,phase:"playable"` or an error.

Failure cases:

| HTTP | error | meaning |
|------|-------|---------|
| 400 | `size_gap` | staged size ≠ `total` (ranges missing; partial kept) |
| 503 | `initializing` | Sony backend still booting, retry shortly |
| 500 | `init_failed` | `sceAppInstUtilInitialize` never succeeded |
| 408 | `upload_timeout` | connection stalled mid-block (partial kept) |
| 400 | `unexpected_eof` | body shorter than `content-length` |
| 507 | `disk_full` | no room for the block |

`staged:"kept"` means the pkg is left on disk for a retry; `notify.pkgnet.*`
toasts fire for receiving / accepted / failed.

#### Single-shot mode (no `offset`/`total`)

Whole pkg as one body:

```http
POST /install?name=MyGame.pkg&wait=120
```

Same install path and response shape as finalize (the old `webui_handle_install`
multipart route that used this is gone — this mode is still reachable via a raw
body).

### `POST /shutdown` — clean exit

`200 {"ok":true,"bye":true,"detail":"shutting down for a fresh injection"}`

Sets the shutdown flag and wakes the accept loops. Used by the takeover flow
and the facade stop path; keep it.

---

## 3) Error shapes

Known path + wrong method:

```json
{"ok":false,"error":"method_not_allowed"}
```

Unknown path (9090):

```json
{"ok":false,"error":"not_found","routes":["GET /ping","POST /install?name=...&wait=120","GET /staged-size?name=x","POST /shutdown"]}
```

Unknown path (12800) is a plain `404`.

---

## 4) Removed endpoints

| Port | Removed endpoint | Superseded by |
|------|------------------|---------------|
| 9090 | `GET /status?content_id=` | `/api/stream` (SSE §1) |
| 9090 | `GET /staged-bytes` | `bytes` field of `/api/stream` |
| 9090 | `GET /progress` | `bytes` field of `/api/stream` |
| 12800 | `GET /api/status` | `/api/stream` |
| 12800 | `POST /api/upload` | chunk `POST /install` (§2) |
| 12800 | `POST /api/install` | chunk `POST /install` (§2) |

The multipart machinery (`webui_write_full`, `webui_stream_multipart_body`)
was deleted together with `/api/upload`.

---

## 5) Development

- The web UI bundle (`source/webui/dist/index.html`) is embedded into the
  pkg-server during the build (`source/util/CMakeLists.txt`); after editing
  `source/webui/src`, rebuild it with `npm run build` in `source/webui`.
- `npm run dev` in `source/webui` serves on `:5173` and proxies `/api` to
  `http://127.0.0.1:8081` (local dev target).
- On the console the bundle is served same-origin on `:12800`, so the SSE URL
  is a relative `/api/stream`; the chunk API base is built from
  `window.location.hostname:9090`.
