/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * The ShellUI-hosted web UI that backs the control-center tile's WebView.
 *
 * The tile's popup renders a native WebViewPS pointed at url().  Everything
 * the page needs is served from this process: static assets out of kRoot, and
 * dynamic endpoints registered through add_json_route().
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace onion::web_ui {

/* Fixed address. 9020/9021 are the ELF loader, 9048 the daemon control
 * channel and 9088 the bootstrapper logger, so the web UI sits above them. */
constexpr uint16_t kPort = 9090;
constexpr const char *kRoot = "/user/data/OnionHEN/web";

/* Returns the complete response body. `query` excludes the leading '?'. */
using RouteHandler = std::function<std::string(std::string_view query)>;

/* Absolute URL for the control-center WebView, e.g. http://127.0.0.1:9090/ */
std::string url();

/* Starts the server, creates kRoot, installs the built-in /api routes and
 * seeds index.html.  A missing index.html is written from the built-in page,
 * and so is one that is still carrying this build's marker; a page someone
 * replaced by hand is left untouched.  Safe to call twice. */
void start();

/* Registers an exact-path GET endpoint. `path` must begin with '/'. */
void add_json_route(const std::string &path, RouteHandler handler);

} // namespace onion::web_ui
