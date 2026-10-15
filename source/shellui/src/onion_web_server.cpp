/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Implementation of the ShellUI-hosted web UI. See onion_web_server.hpp.
 */
#include "onion_web_server.hpp"

#include "webserver.hpp"

#include <onion/log.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <utility>
#include <vector>

namespace onion::web_ui {
namespace {

std::mutex g_mutex;
/*
 * Deliberately leaked, not owned by a unique_ptr: the accept loop runs on a
 * detached pthread (see webserver.hpp), so the server object has to outlive
 * every static destructor and stay valid until the shell process itself goes
 * away.  There is exactly one of these per process.
 */
SimpleHTTPServer *g_server = nullptr;
/*
 * add_json_route() is part of the public surface and callers may register
 * before start() runs, so routes land here first and are flushed onto the
 * server the moment it exists.
 */
std::vector<std::pair<std::string, RouteHandler>> g_pending_routes;

/*
 * Marker carried by every page this build writes.  seed_default_index() uses
 * it to tell "a default we seeded earlier" from "a page the user edited by
 * hand": only the former is overwritten, so bumping the built-in page reaches
 * consoles that already seeded an older one without ever clobbering custom
 * content dropped into kRoot.
 */
constexpr char kDefaultIndexMarker[] = "<!-- onionhen-web-ui -->";

/*
 * The control-center popup's WebView loads url(), so this is the first thing
 * the tile shows.  The panel is 652x640 with `backgroundTransparencyEnabled`
 * set, so the page keeps its canvas transparent and lets the popup's own
 * `rgba(8, 10, 15, 1.0)` mat show through instead of painting a second
 * background on top of it.
 */
constexpr char kDefaultIndex[] = R"HTML(<!doctype html>
<!-- onionhen-web-ui -->
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OnionHEN</title>
<style>
html,body{height:100%}
body{margin:0;display:flex;align-items:center;justify-content:center;
background:transparent;color:#e8e8ea;text-align:center;letter-spacing:.01em;
font-family:system-ui,sans-serif;font-weight:600;line-height:1.25;
font-size:44px;font-size:clamp(22px,6.5vw,46px)}
</style></head>
<body>Hello From OnionHEN</body></html>
)HTML";

/*
 * The page has to carry the marker verbatim or seed_default_index() would stop
 * recognising its own output and never refresh it.  Catch that at build time
 * rather than by staring at a stale page on the console.
 */
static_assert(std::string_view(kDefaultIndex, sizeof(kDefaultIndex) - 1)
                  .find(kDefaultIndexMarker) != std::string_view::npos,
              "the built-in page must contain kDefaultIndexMarker");

void make_directories(const std::string &path) {
  std::string partial;
  for (size_t i = 0; i < path.size(); ++i) {
    partial.push_back(path[i]);
    if (path[i] == '/' && partial.size() > 1) {
      mkdir(partial.c_str(), 0755);
    }
  }
  if (!partial.empty() && partial.back() != '/')
    mkdir(partial.c_str(), 0755);
}

/*
 * True when index.html is already there and is still one of ours.  Only the
 * first 511 bytes are read, which is enough for the marker on line 2.
 */
bool is_seeded_default(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;
  char head[512] = {};
  in.read(head, sizeof(head) - 1);
  return strstr(head, kDefaultIndexMarker) != nullptr;
}

void seed_default_index() {
  const std::string index = std::string(kRoot) + "/index.html";
  struct stat info {};
  if (stat(index.c_str(), &info) == 0 && !is_seeded_default(index))
    return; // Someone replaced it: their page wins over ours.
  std::ofstream out(index, std::ios::binary | std::ios::trunc);
  if (!out)
    return;
  out.write(kDefaultIndex, sizeof(kDefaultIndex) - 1);
  LOG_DEBUG("web_ui: seeded default %s", index.c_str());
}

std::string json_status(std::string_view) {
  char buffer[192];
  snprintf(buffer, sizeof(buffer),
           "{\"ok\":true,\"name\":\"OnionHEN\",\"port\":%u,"
           "\"root\":\"%s\"}",
           static_cast<unsigned>(kPort), kRoot);
  return std::string(buffer);
}

std::string json_ping(std::string_view) {
  return std::string("{\"pong\":true}");
}

} // namespace

std::string url() {
  return std::string("http://127.0.0.1:") + std::to_string(kPort) + "/";
}

void add_json_route(const std::string &path, RouteHandler handler) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_server) {
    /* Not started yet: keep it until start() flushes the list. */
    g_pending_routes.emplace_back(path, std::move(handler));
    return;
  }
  g_server->add_route(path, "application/json; charset=utf-8",
                      std::move(handler));
}

void start() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_server)
    return;

  make_directories(kRoot);
  seed_default_index();

  g_server = new SimpleHTTPServer(static_cast<int>(kPort), kRoot);
  g_server->add_route("/api/status", "application/json; charset=utf-8",
                      json_status);
  g_server->add_route("/api/ping", "application/json; charset=utf-8",
                      json_ping);
  /* Routes registered before the server existed now have somewhere to go. */
  for (auto &pending : g_pending_routes) {
    g_server->add_route(pending.first, "application/json; charset=utf-8",
                        std::move(pending.second));
  }
  g_pending_routes.clear();
  g_server->start();

  LOG_DEBUG("web_ui: serving %s from %s", url().c_str(), kRoot);
}

} // namespace onion::web_ui
