/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Adapter boundary for the vendored DPI package install server
 * (third_party/pkgserver). The service facade owns the worker thread and
 * lifecycle; the server keeps its standalone JSON API untouched.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Run the accept/serve loop; blocks until pkg_server_request_stop(). */
int pkg_server_main(void);

/** Reset one-shot state before a new serving session. */
void pkg_server_prepare(void);

/** Ask the serving loop to exit (flag + wake the accept socket). */
void pkg_server_request_stop(void);

/** True once the listener is bound and not stopping. */
int pkg_server_is_listening(void);

/** UI language code advertised by the status stream (e.g. "en", "es"). */
void pkg_server_set_webui_lang(const char *lang);

#ifdef __cplusplus
}
#endif
