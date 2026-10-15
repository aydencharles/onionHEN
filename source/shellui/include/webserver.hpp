/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Minimal blocking HTTP/1.1 file server used by the ShellUI Toolbox web UI.
 *
 * Two layers, checked in this order for every GET:
 *   1. the route table - exact path matches registered through add_route(),
 *      which is how OnionHEN serves dynamic JSON or generated HTML;
 *   2. the static root  - anything else is read from root_dir, with "/"
 *      mapping to "/index.html".
 *
 * Range requests are honoured for the static layer so the WebView can stream
 * media and resume downloads.
 *
 * ---------------------------------------------------------------------------
 * The accept loop runs on a raw pthread, NOT on std::thread.
 *
 * libc++'s std::thread starts every new thread by installing a
 * __thread_specific_ptr<__thread_struct>, which bottoms out in
 * pthread_setspecific().  That call is not usable in this payload: the first
 * std::thread OnionHEN ever created killed SceShellUI with SIGSEGV - the
 * signal landed in libkernel_sys.sprx's syscall stub for 0x259, reached from
 * __libcpp_tls_set (see notes/RUNTIME-INJECTION.md 15).  Every other thread in
 * this repository is created with pthread_create(), and so is this one.
 *
 * Rules for anything that runs on the server thread:
 *   - no std::thread / std::jthread,
 *   - no exceptions - __cxa_get_globals also goes through pthread_setspecific,
 *   - no thread_local - emulated TLS uses the same pthread key path.
 * The body below is written to those rules: request parsing is strtol based
 * and the response headers are formatted with snprintf instead of
 * std::ostringstream.
 * ---------------------------------------------------------------------------
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <pthread.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <vector>

#include <onion/log.h>

class SimpleHTTPServer {
public:
    /* Returns the complete response body. `query` excludes the leading '?'. */
    using RouteHandler = std::function<std::string(std::string_view query)>;

    struct Route {
        std::string path;
        std::string content_type;
        RouteHandler handler;
    };

private:
    int port;
    std::string root_dir;
    pthread_t server_thread {};
    bool thread_started = false;
    int listen_socket = -1;
    bool running;
    std::vector<Route> routes;
    std::mutex routes_mutex;

    static std::string getMimeType(const std::string& path) {
        if (path.find(".html") != std::string::npos) return "text/html; charset=utf-8";
        if (path.find(".css") != std::string::npos) return "text/css; charset=utf-8";
        if (path.find(".js") != std::string::npos) return "application/javascript; charset=utf-8";
        if (path.find(".json") != std::string::npos) return "application/json; charset=utf-8";
        if (path.find(".svg") != std::string::npos) return "image/svg+xml";
        if (path.find(".png") != std::string::npos) return "image/png";
        if (path.find(".jpg") != std::string::npos ||
            path.find(".jpeg") != std::string::npos) return "image/jpeg";
        if (path.find(".webp") != std::string::npos) return "image/webp";
        if (path.find(".woff2") != std::string::npos) return "font/woff2";
        if (path.find(".mp4") != std::string::npos) return "video/mp4";
        return "application/octet-stream";
    }

    static long getFileSize(const std::string& path) {
        struct stat stat_buf;
        int rc = stat(path.c_str(), &stat_buf);
        return rc == 0 ? static_cast<long>(stat_buf.st_size) : -1;
    }

    /* strtol rather than std::stol: std::stol throws on malformed input and
     * this runs on the server thread, where throwing is not an option. */
    static bool parseLong(const std::string& text, long& out) {
        if (text.empty()) return false;
        char* end = nullptr;
        const long value = strtol(text.c_str(), &end, 10);
        if (end == text.c_str() || *end != '\0' || value < 0) return false;
        out = value;
        return true;
    }

    static void parseRange(const std::string& range_header, long file_size,
        long& start, long& end) {
        start = 0;
        end = file_size > 0 ? file_size - 1 : 0;

        if (range_header.empty() || file_size <= 0) return;

        std::string range = range_header.substr(6); // Remove "bytes="
        size_t dash_pos = range.find('-');
        if (dash_pos == std::string::npos) return;

        long parsed = 0;
        if (parseLong(range.substr(0, dash_pos), parsed)) start = parsed;
        if (parseLong(range.substr(dash_pos + 1), parsed)) end = parsed;

        if (end >= file_size) end = file_size - 1;
        if (start > end) start = end;
    }

    static void sendAll(int client_socket, const char* data, size_t size) {
        size_t sent = 0;
        while (sent < size) {
            const ssize_t n = send(client_socket, data + sent, size - sent, 0);
            if (n <= 0) return;
            sent += static_cast<size_t>(n);
        }
    }

    static void sendBody(int client_socket, const char* status,
        const char* content_type, const char* body, size_t body_size) {
        char head[256];
        int n = snprintf(head, sizeof(head),
            "HTTP/1.1 %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n\r\n",
            status, content_type, body_size);
        if (n < 0) return;
        if (static_cast<size_t>(n) >= sizeof(head)) n = sizeof(head) - 1;
        sendAll(client_socket, head, static_cast<size_t>(n));
        if (body_size > 0) sendAll(client_socket, body, body_size);
    }

    static void sendBody(int client_socket, const char* status,
        const std::string& content_type, const std::string& body) {
        sendBody(client_socket, status, content_type.c_str(), body.data(),
            body.size());
    }

    void sendFile(int client_socket, const std::string& file_path,
        const std::string& range_header) {
        std::ifstream file(file_path.c_str(), std::ios::binary);
        if (!file) {
            static const char kNotFound[] = "Not Found";
            sendBody(client_socket, "404 Not Found", "text/plain; charset=utf-8",
                kNotFound, sizeof(kNotFound) - 1);
            return;
        }

        const long file_size = getFileSize(file_path);
        if (file_size <= 0) {
            static const char kEmpty[] = "Not Found";
            sendBody(client_socket, "404 Not Found", "text/plain; charset=utf-8",
                kEmpty, sizeof(kEmpty) - 1);
            return;
        }

        long start = 0;
        long end = 0;
        parseRange(range_header, file_size, start, end);

        const long content_length = end - start + 1;
        const std::string mime_type = getMimeType(file_path);

        char head[384];
        int n = 0;
        if (range_header.empty()) {
            n = snprintf(head, sizeof(head),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %ld\r\n"
                "Accept-Ranges: bytes\r\n"
                "Connection: close\r\n\r\n",
                mime_type.c_str(), content_length);
        }
        else {
            n = snprintf(head, sizeof(head),
                "HTTP/1.1 206 Partial Content\r\n"
                "Content-Range: bytes %ld-%ld/%ld\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %ld\r\n"
                "Accept-Ranges: bytes\r\n"
                "Connection: close\r\n\r\n",
                start, end, file_size, mime_type.c_str(), content_length);
        }
        if (n < 0) return;
        if (static_cast<size_t>(n) >= sizeof(head)) n = sizeof(head) - 1;
        sendAll(client_socket, head, static_cast<size_t>(n));

        file.seekg(start);
        char buffer[8192];
        long bytes_to_send = content_length;

        while (bytes_to_send > 0 && file) {
            long chunk_size = std::min(bytes_to_send, (long)sizeof(buffer));
            file.read(buffer, chunk_size);
            long bytes_read = static_cast<long>(file.gcount());

            if (bytes_read > 0) {
                sendAll(client_socket, buffer, static_cast<size_t>(bytes_read));
                bytes_to_send -= bytes_read;
            }
            else {
                break;
            }
        }
    }

    bool dispatchRoute(int client_socket, const std::string& path,
        const std::string& query) {
        RouteHandler handler;
        std::string content_type;
        {
            std::lock_guard<std::mutex> lock(routes_mutex);
            for (const auto& route : routes) {
                if (route.path == path) {
                    handler = route.handler;
                    content_type = route.content_type;
                    break;
                }
            }
        }
        if (!handler) return false;

        /* Best effort only: the unwinder itself needs the same pthread key
         * path that is broken here, so a handler that really throws still
         * takes the shell down.  Registered handlers must not throw. */
        std::string body;
        try {
            body = handler(query);
        }
        catch (...) {
            static const char kFailed[] = "Handler failed";
            sendBody(client_socket, "500 Internal Server Error",
                "text/plain; charset=utf-8", kFailed, sizeof(kFailed) - 1);
            return true;
        }
        sendBody(client_socket, "200 OK", content_type, body);
        return true;
    }

    void handleRequest(int client_socket) {
        char buffer[4096] = { 0 };
        const ssize_t received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) return;

        std::string request(buffer, static_cast<size_t>(received));

        /* "GET /path?query HTTP/1.1" - parsed by hand, see the file header. */
        const size_t method_end = request.find(' ');
        if (method_end == std::string::npos) {
            static const char kBad[] = "Bad Request";
            sendBody(client_socket, "400 Bad Request", "text/plain; charset=utf-8",
                kBad, sizeof(kBad) - 1);
            return;
        }
        const size_t target_end = request.find(' ', method_end + 1);
        if (target_end == std::string::npos) {
            static const char kBad[] = "Bad Request";
            sendBody(client_socket, "400 Bad Request", "text/plain; charset=utf-8",
                kBad, sizeof(kBad) - 1);
            return;
        }

        const std::string method = request.substr(0, method_end);
        if (method != "GET") {
            static const char kNotAllowed[] = "Method Not Allowed";
            sendBody(client_socket, "405 Method Not Allowed",
                "text/plain; charset=utf-8", kNotAllowed,
                sizeof(kNotAllowed) - 1);
            return;
        }

        std::string range_header;
        size_t range_pos = request.find("Range: bytes=");
        if (range_pos != std::string::npos) {
            size_t end_pos = request.find("\r\n", range_pos);
            if (end_pos != std::string::npos) {
                range_header = request.substr(range_pos + 7,
                    end_pos - range_pos - 7);
            }
        }

        /* Split "/path?query" and reject anything trying to escape the root. */
        std::string path = request.substr(method_end + 1,
            target_end - method_end - 1);
        std::string query;
        const size_t mark = path.find('?');
        if (mark != std::string::npos) {
            query = path.substr(mark + 1);
            path = path.substr(0, mark);
        }
        if (path.find("..") != std::string::npos) {
            static const char kBad[] = "Bad Request";
            sendBody(client_socket, "400 Bad Request", "text/plain; charset=utf-8",
                kBad, sizeof(kBad) - 1);
            return;
        }

        if (dispatchRoute(client_socket, path, query)) return;

        if (path == "/") path = "/index.html";
        sendFile(client_socket, root_dir + path, range_header);
    }

    void serverLoop() {
        LOG_DEBUG("web_ui: server thread entered");

        const int server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket < 0) {
            LOG_ERROR("web_ui: socket() failed");
            thread_started = false;
            return;
        }
        listen_socket = server_socket;

        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(static_cast<uint16_t>(port));

        if (bind(server_socket, (struct sockaddr*)&server_addr,
            sizeof(server_addr)) < 0) {
            LOG_ERROR("web_ui: bind(:%d) failed", port);
            close(server_socket);
            listen_socket = -1;
            thread_started = false;
            return;
        }

        if (listen(server_socket, 10) < 0) {
            LOG_ERROR("web_ui: listen(:%d) failed", port);
            close(server_socket);
            listen_socket = -1;
            thread_started = false;
            return;
        }

        running = true;
        LOG_DEBUG("web_ui: listening on port %d root=%s", port,
            root_dir.c_str());

        while (running) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            int client_socket = accept(server_socket,
                (struct sockaddr*)&client_addr,
                &client_len);

            if (client_socket >= 0) {
                handleRequest(client_socket);
                close(client_socket);
            }
            else if (!running) {
                break;
            }
        }

        close(server_socket);
        listen_socket = -1;
        thread_started = false;
        LOG_DEBUG("web_ui: server thread left");
    }

    static void* threadEntry(void* self) {
        static_cast<SimpleHTTPServer*>(self)->serverLoop();
        return nullptr;
    }

    static std::string getLocalIP() {
        /* The WebView runs in the same process tree as ShellUI. */
        return "127.0.0.1";
    }

public:
    SimpleHTTPServer(int p, const std::string& root)
        : port(p), root_dir(root), running(false) {
    }

    ~SimpleHTTPServer() {
        stop();
    }

    SimpleHTTPServer(const SimpleHTTPServer&) = delete;
    SimpleHTTPServer& operator=(const SimpleHTTPServer&) = delete;

    /* Exact-path GET route. Overrides the static layer for that path. */
    void add_route(const std::string& path, const std::string& content_type,
        RouteHandler handler) {
        std::lock_guard<std::mutex> lock(routes_mutex);
        for (auto& route : routes) {
            if (route.path == path) {
                route.content_type = content_type;
                route.handler = std::move(handler);
                return;
            }
        }
        routes.push_back({path, content_type, std::move(handler)});
    }

    void start() {
        if (thread_started) return;
        thread_started = true;
        const int rc = pthread_create(&server_thread, nullptr,
            &SimpleHTTPServer::threadEntry, this);
        if (rc != 0) {
            thread_started = false;
            LOG_ERROR("web_ui: pthread_create failed rc=%d", rc);
            return;
        }
        /* The server lives as long as the shell does; nobody joins it. */
        pthread_detach(server_thread);
    }

    /* Best effort: asks the loop to leave accept().  The thread is detached,
     * so there is nothing to join - the owner must keep this object alive for
     * the rest of the process lifetime. */
    void stop() {
        running = false;
        /* Unblock accept() so the loop can observe `running`. */
        if (listen_socket >= 0) ::shutdown(listen_socket, SHUT_RDWR);
    }

    bool isRunning() const {
        return running;
    }

    int boundPort() const {
        return port;
    }

    std::string getURL() const {
        return "http://" + getLocalIP() + ":" + std::to_string(port);
    }
};
