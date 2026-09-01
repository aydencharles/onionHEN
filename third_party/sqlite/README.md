# sqlite

Vendored SQLite amalgamation, version **3.53.4** (`sqlite-amalgamation-3530400`).

| File | Role in OnionHEN |
|------|------------------|
| `sqlite3.c` / `sqlite3.h` | App database used by the embedded ShadowMount+ module |

Upstream: <https://www.sqlite.org/download.html> (public domain; see the
license banner inside `sqlite3.c`). Only the amalgamation is vendored; compile
flags and warning relaxations live in `source/util/CMakeLists.txt`.
