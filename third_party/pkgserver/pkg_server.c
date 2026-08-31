/*
 * pkg-server.elf - PS5 PKG install server (TCP :9090, JSON API).
 *
 * Upload a .pkg, server stages it under /user/data/tmp and calls
 * sceAppInstUtilInstallByPackage (SYSTEM authid). Sony's IPMI backend
 * handles the actual installation.
 *
 *   GET  /ping                        liveness + busy probe (takeover)
 *   POST /install?name=x&offset=&total=  chunk upload (multi-stream)
 *   POST /install?...&finalize=1      install already-staged pkg
 *   POST /install?name=x.pkg          single-shot upload + install
 *   GET  /staged-size?name=x          staged file size (reuse probe)
 *   POST /shutdown                    clean exit
 *
 * Progress/status for the web UI is a separate SSE port (:12800,
 * /api/stream); the legacy /status and /staged-bytes pollers are gone.
 *
 * OnionHEN: compiled into util.elf as the network package installer (DPI)
 * service; pkg_server_main runs on the facade worker thread.
 *
 * Credits: LightningMods (etaHEN), cy33hc (ezRemote-DPI),
 * soniciso/elf-arsenal (jb_escalate), ps5upload (authid matrix) & kvnhrt
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include "pkgserver_sha256.h"
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <ps5/kernel.h>

#define ONION_NOTIFY_NO_LEGACY_MACRO 1
#include <onion/notify.h>
#include <onion/notify_i18n.h>
#include <onion/builtin_services.h>

#include "multipart.h"
#include "sceAppInstUtil.h"
#include "authid.h"
#include "timed_init.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif


#define SERVER_PORT       ONION_PKGNET_PORT
#define WEBUI_PORT        ONION_WEBUI_PORT
#define VERSION           "1.3.2"
#define BACKLOG           32

#define HDR_BUF_CAP       16384L
#define QUERY_MAX         512
#define NAME_MAX_LEN      96
#define BODY_CHUNK        4194304
#define HEAD_IDENTITY_BYTES 65536u
#define HEAD_REUSE_BYTES  1048576u

#define RECV_TIMEOUT_S    30
#define INIT_TIMEOUT_MS   10000U

#define TMP_DIR           "/user/data/tmp"

/* Web UI single-file bundle (Vite + vite-plugin-singlefile). Embedded at
 * build time; the CMake ONIONHEN_WEBUI_INDEX define carries the absolute
 * dist/index.html path. */
#ifdef ONIONHEN_WEBUI_INDEX
__asm__(
    ".pushsection .rodata\n"
    ".balign 1\n"
    ".global onion_webui_index_start\n"
    "onion_webui_index_start:\n"
    ".incbin \"" ONIONHEN_WEBUI_INDEX "\"\n"
    ".global onion_webui_index_end\n"
    "onion_webui_index_end:\n"
    ".popsection\n");
extern const unsigned char onion_webui_index_start[];
extern const unsigned char onion_webui_index_end[];
#else
static const char kWebuiIndexFallback[] =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<title>OnionHEN Package Installer</title></head>"
    "<body>WebUI bundle not embedded in this build.</body></html>\n";
#endif

static const unsigned char *webui_index_data(void) {
#ifdef ONIONHEN_WEBUI_INDEX
    return onion_webui_index_start;
#else
    return (const unsigned char *)kWebuiIndexFallback;
#endif
}

static size_t webui_index_size(void) {
#ifdef ONIONHEN_WEBUI_INDEX
    return (size_t)(onion_webui_index_end - onion_webui_index_start);
#else
    return sizeof(kWebuiIndexFallback) - 1;
#endif
}


typedef struct {
    int32_t error_code;
    int32_t version;
    char    description[512];
    char    type[9];
} appinst_err_info_t;

typedef struct {
    char     status[16];
    char     src_type[8];
    uint32_t remain_time;
    uint64_t downloaded_size;
    uint64_t initial_chunk_size;
    uint64_t total_size;
    uint32_t promote_progress;
    appinst_err_info_t error_info;
    int32_t  local_copy_percent;
    int      is_copy_only;
} appinst_status_t;

extern int sceAppInstUtilGetInstallStatus(const char *content_id,
                                          appinst_status_t *out);

extern int sceKernelSetProcessName(const char *name);

/* Toasts: i18n text via onion_notify_tr, sent through the direct
 * sceKernelSendNotificationRequest syscall (blocking=0) — the same path the
 * standalone pkg-server has always used. Avoids the process-wide
 * onion_notify()/g_send indirection from these server threads. */
typedef struct pkg_notify_request {
    char useless1[45];
    char message[3075];
} pkg_notify_request_t;

extern int sceKernelSendNotificationRequest(int device,
                                            pkg_notify_request_t *req,
                                            size_t size, int blocking);

static void pkg_notify(const char *key, ...) {
    pkg_notify_request_t req;
    va_list ap;
    memset(&req, 0, sizeof(req));
    va_start(ap, key);
    vsnprintf(req.message, sizeof(req.message), onion_notify_tr(key), ap);
    va_end(ap);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}


#define LOG_FILE_PATH "/data/OnionHEN/pkg-server.log"


static void log_line(const char *fmt, ...) {
    va_list ap, ap2;
    struct timespec ts;
    struct tm tmv;

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tmv);

    fprintf(stderr, "[%02d:%02d:%02d] ", tmv.tm_hour, tmv.tm_min,
            tmv.tm_sec);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);

    FILE *f = fopen(LOG_FILE_PATH, "a");
    if (!f)
        return;
    fprintf(f, "[%02d:%02d:%02d] ", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    va_start(ap, fmt);
    va_copy(ap2, ap);
    vfprintf(f, fmt, ap2);
    va_end(ap2);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}




static pthread_mutex_t g_op_mtx = PTHREAD_MUTEX_INITIALIZER;

static ps5_timed_init_state_t g_init_state = PS5_TIMED_INIT_STATE_INITIALIZER;
static int g_init_rc = -1;
static int g_fw_major = 0;


static int g_srvfd = -1;
static volatile int g_shutdown = 0;
static volatile int g_shutdown_backoff = 0;

static uint64_t g_rx_bytes;

static void rx_bytes_add(uint64_t n) {
    __atomic_fetch_add(&g_rx_bytes, n, __ATOMIC_RELAXED);
}

static void rx_bytes_reset(void) {
    __atomic_store_n(&g_rx_bytes, 0, __ATOMIC_RELAXED);
}

static uint64_t rx_bytes_get(void) {
    return __atomic_load_n(&g_rx_bytes, __ATOMIC_RELAXED);
}

typedef struct {
    int      in_use;
    uint64_t seq;
    char     content_id[CONTENTID_SIZE];
    char     tmp_path[512];
    
    int      terminal;
    char     phase[16];
    uint64_t downloaded_size;
    uint64_t total_size;
    uint32_t promote_progress;
    unsigned int error_code;
    uint64_t snap_ms;
    int      toast_done;
} task_slot_t;

static uint64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull +
           (uint64_t)ts.tv_nsec / 1000000ull;
}

static int is_terminal_status(const appinst_status_t *st) {
    return strncmp(st->status, "playable", 8) == 0 ||
           strncmp(st->status, "broken", 6) == 0 ||
           strncmp(st->status, "none", 4) == 0 ||
           st->error_info.error_code != 0;
}

#define TASK_TABLE 8
static task_slot_t g_tasks[TASK_TABLE];
static pthread_mutex_t g_tasks_mtx = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_task_seq;

/* Web UI state: the 12800 listener serves the single-file bundle and the
 * /api/* endpoints for the browser flow (stage → install → poll). */
static volatile int g_webui_fd = -1;
static pthread_t g_webui_thread;
static int g_webui_thread_created = 0;

/* Web UI state machine:
 *   WEBUI_IDLE       - nothing happening
 *   WEBUI_UPLOADING  - multipart upload in progress
 *   WEBUI_STAGED     - upload complete, pkg staged, ready for install
 *   WEBUI_INSTALLING - install_staged running (finalize)
 *   WEBUI_COMPLETE   - install finished successfully (playable)
 *   WEBUI_ERROR      - install failed
 */
typedef enum {
    WEBUI_IDLE = 0,
    WEBUI_UPLOADING,
    WEBUI_STAGED,
    WEBUI_INSTALLING,
    WEBUI_COMPLETE,
    WEBUI_ERROR
} webui_state_t;

#define WEBUI_TERM_RESET_MS 5000
static volatile int g_webui_state = WEBUI_IDLE;
#define WEBUI_LANG_MAX 16
static char g_webui_lang[WEBUI_LANG_MAX] = "en";
static pthread_mutex_t g_webui_lang_mtx = PTHREAD_MUTEX_INITIALIZER;
static char g_webui_staged_name[NAME_MAX_LEN + 8];
static uint64_t g_webui_staged_size;
static int g_webui_staged;
static char g_webui_last_cid[CONTENTID_SIZE];
static char g_webui_cur_name[NAME_MAX_LEN + 8];
static uint64_t g_webui_cur_total;



void webui_set_state(webui_state_t state) {
    g_webui_state = state;
}


static int jb_escalate_pid(pid_t pid) {
    if (pid <= 0)
        return -1;

    intptr_t proc = kernel_get_proc(pid);
    if (!proc)
        return -1;

    int rc = 0;

    if (kernel_set_ucred_uid(pid, 0) != 0) rc = -1;
    if (kernel_set_ucred_ruid(pid, 0) != 0) rc = -1;
    if (kernel_set_ucred_svuid(pid, 0) != 0) rc = -1;
    if (kernel_set_ucred_rgid(pid, 0) != 0) rc = -1;
    if (kernel_set_ucred_svgid(pid, 0) != 0) rc = -1;

    intptr_t rootvnode = kernel_get_root_vnode();
    if (rootvnode) {
        if (kernel_set_proc_rootdir(pid, rootvnode) != 0) rc = -1;
        if (kernel_set_proc_jaildir(pid, rootvnode) != 0) rc = -1;
    }

    
    if (kernel_set_ucred_authid(pid, PS5_JB_AUTHID) != 0) rc = -1;

    uint8_t caps[16];
    memset(caps, 0xff, sizeof(caps));
    if (kernel_set_ucred_caps(pid, caps) != 0) rc = -1;

    uint8_t attrs[32];
    memset(attrs, 0, sizeof(attrs));
    attrs[0] = 0x80;
    if (kernel_set_ucred_attrs(pid, attrs) != 0) rc = -1;

    return rc;
}


static int ensure_init_locked(void) {
    if (g_init_rc == 0)
        return 0;
    g_init_rc = ps5_timed_init_wait(&g_init_state, sceAppInstUtilInitialize,
                                    INIT_TIMEOUT_MS);
    log_line("sceAppInstUtilInitialize -> 0x%08X", (unsigned)g_init_rc);
    return g_init_rc;
}


#define KINFO_PID_OFFSET    72
#define KINFO_TDNAME_OFFSET 447




static int do_install(const char *uri,
                      char *out_cid, size_t cid_cap, const char **out_via,
                      int ctype_hint) {
    MetaInfo meta;
    SceAppInstallPkgInfo pkg_info;
    PlayGoInfo playgo;

    memset(&meta, 0, sizeof(meta));
    memset(&pkg_info, 0, sizeof(pkg_info));
    memset(&playgo, 0, sizeof(playgo));

    if (ctype_hint > 0)
        pkg_info.content_type = ctype_hint;

    meta.uri = uri;
    meta.ex_uri = "";
    meta.playgo_scenario_id = "";
    meta.content_id = "";
    meta.content_name = uri;
    meta.icon_url = "";

    int rc = sceAppInstUtilInstallByPackage(&meta, &pkg_info, &playgo);
    log_line("install -> 0x%08X", (unsigned)rc);

    if (rc == 0 && out_cid && cid_cap > 0) {
        strncpy(out_cid, pkg_info.content_id, cid_cap - 1);
        out_cid[cid_cap - 1] = '\0';
    }
    if (out_via)
        *out_via = (rc == 0) ? "system" : "none";
    log_line("install summary: rc=0x%08X via=%s uri=%s", (unsigned)rc,
             (rc == 0) ? "system" : "none", uri);
    return rc;
}


static void task_add(const char *cid, const char *tmp_path) {
    pthread_mutex_lock(&g_tasks_mtx);

    task_slot_t *slot = NULL;

    
    for (int i = 0; i < TASK_TABLE && !slot; i++) {
        if (g_tasks[i].in_use && cid && cid[0] &&
            strcmp(g_tasks[i].content_id, cid) == 0)
            slot = &g_tasks[i];
    }
    if (!slot) {
        for (int i = 0; i < TASK_TABLE && !slot; i++) {
            if (!g_tasks[i].in_use)
                slot = &g_tasks[i];
        }
    }
    if (!slot) {
        
        slot = &g_tasks[0];
        for (int i = 1; i < TASK_TABLE; i++) {
            if (g_tasks[i].seq < slot->seq)
                slot = &g_tasks[i];
        }
    }

    memset(slot, 0, sizeof(*slot));
    slot->in_use = 1;
    slot->seq = ++g_task_seq;
    strncpy(slot->content_id, cid ? cid : "",
            sizeof(slot->content_id) - 1);
    strncpy(slot->tmp_path, tmp_path ? tmp_path : "",
            sizeof(slot->tmp_path) - 1);
    strncpy(g_webui_last_cid, cid ? cid : "",
            sizeof(g_webui_last_cid) - 1);

    pthread_mutex_unlock(&g_tasks_mtx);
}

static int task_lookup(const char *cid, task_slot_t *out) {
    pthread_mutex_lock(&g_tasks_mtx);
    int found = 0;
    if (cid && cid[0]) {
        for (int i = 0; i < TASK_TABLE; i++) {
            if (g_tasks[i].in_use &&
                strcmp(g_tasks[i].content_id, cid) == 0) {
                *out = g_tasks[i];
                found = 1;
                break;
            }
        }
    } else {
        
        task_slot_t *best = NULL;
        for (int i = 0; i < TASK_TABLE; i++) {
            if (g_tasks[i].in_use && (!best || g_tasks[i].seq > best->seq))
                best = &g_tasks[i];
        }
        if (best) {
            *out = *best;
            found = 1;
        }
    }
    pthread_mutex_unlock(&g_tasks_mtx);
    return found;
}


static void task_store_snapshot(const char *cid, const appinst_status_t *st) {
    pthread_mutex_lock(&g_tasks_mtx);
    for (int i = 0; i < TASK_TABLE; i++) {
        if (!g_tasks[i].in_use || strcmp(g_tasks[i].content_id, cid) != 0)
            continue;
        int was_terminal = g_tasks[i].terminal;
        strncpy(g_tasks[i].phase, st->status,
                sizeof(g_tasks[i].phase) - 1);
        g_tasks[i].downloaded_size = st->downloaded_size;
        g_tasks[i].total_size = st->total_size;
        g_tasks[i].promote_progress = st->promote_progress;
        g_tasks[i].error_code = (unsigned int)st->error_info.error_code;
        g_tasks[i].snap_ms = mono_ms();
        if (is_terminal_status(st))
            g_tasks[i].terminal = 1;

        
        if (g_tasks[i].terminal && !was_terminal && !g_tasks[i].toast_done) {
            g_tasks[i].toast_done = 1;
            pthread_mutex_unlock(&g_tasks_mtx);
pkg_notify("notify.pkg.finished");
            return;
        }
        break;
    }
    pthread_mutex_unlock(&g_tasks_mtx);
}




#define INSTALL_WAIT_MAX_S     600
#define INSTALL_POLL_S         2
#define TAKEOVER_BUSY_GRACE_S 45
#define STATUS_FRESH_MS 3000ull

static int wait_terminal_locked(const char *cid, int max_s,
                                appinst_status_t *out) {
    struct timespec ts = {INSTALL_POLL_S, 0};

    for (int s = 0; s < max_s; s += INSTALL_POLL_S) {
        nanosleep(&ts, NULL);

        memset(out, 0, sizeof(*out));
        int rc = sceAppInstUtilGetInstallStatus(cid, out);
        if (rc != 0)
            continue;

        log_line("wait[%s] phase=%s dl=%llu/%llu err=0x%08X", cid,
                 out->status, (unsigned long long)out->downloaded_size,
                 (unsigned long long)out->total_size,
                 (unsigned)out->error_info.error_code);
        task_store_snapshot(cid, out);

        if (is_terminal_status(out))
            return 1;
    }
    return 0;
}


static void json_escape(const char *in, char *out, size_t cap) {
    size_t o = 0;
    for (; in && *in && o + 7 < cap; in++) {
        unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c < 0x20) {
            int w = snprintf(out + o, cap - o, "\\u%04x", c);
            if (w <= 0)
                break;
            o += (size_t)w;
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

static void hex32(char *out, size_t cap, unsigned int v) {
    snprintf(out, cap, "0x%08X", v);
}


static void send_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, MSG_NOSIGNAL);
        if (n <= 0)
            return;
        off += (size_t)n;
    }
}

static void send_json(int fd, int code, const char *reason,
                      const char *json) {
    char hdr[256];
    size_t body_len = strlen(json);
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %zu\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     code, reason, body_len);
    if (n <= 0 || (size_t)n >= sizeof(hdr))
        return;
    send_all(fd, hdr, (size_t)n);
    send_all(fd, json, body_len);
}

static void percent_decode(const char *src, char *dst, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; src[i] && o + 1 < cap; i++) {
        if (src[i] == '%' && isxdigit((unsigned char)src[i + 1]) &&
            isxdigit((unsigned char)src[i + 2])) {
            char hex[3] = { src[i + 1], src[i + 2], '\0' };
            dst[o++] = (char)strtoul(hex, NULL, 16);
            i += 2;
        } else {
            dst[o++] = src[i];
        }
    }
    dst[o] = '\0';
}

static int query_get(const char *query, const char *key, char *out,
                     size_t outcap) {
    if (!query)
        return 0;
    size_t klen = strlen(key);
    const char *p = query;
    while (*p) {
        const char *amp = strchr(p, '&');
        size_t seglen = amp ? (size_t)(amp - p) : strlen(p);
        if (seglen > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            char raw[1024];
            size_t vlen = seglen - klen - 1;
            if (vlen >= sizeof(raw))
                vlen = sizeof(raw) - 1;
            memcpy(raw, p + klen + 1, vlen);
            raw[vlen] = '\0';
            percent_decode(raw, out, outcap);
            return 1;
        }
        if (!amp)
            break;
        p = amp + 1;
    }
    return 0;
}


static int sanitize_pkg_name(const char *in, char *out, size_t cap) {
    size_t n = in ? strlen(in) : 0;
    if (n == 0 || n > NAME_MAX_LEN)
        return -1;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0x20 || c > 0x7e)
            return -1;
        if (c == '/' || c == '\\' || c == '"' || c == '\'' || c == ';' ||
            c == '&' || c == '|' || c == '$' || c == '`' || c == '<' ||
            c == '>')
            return -1;
    }
    if (strstr(in, ".."))
        return -1;
    if (cap < n + 5)
        return -1;
    strcpy(out, in);
    size_t ol = strlen(out);
    if (ol < 4 || strcasecmp(out + ol - 4, ".pkg") != 0)
        strcat(out, ".pkg");
    return 0;
}


static int read_headers(int fd, char *buf, size_t cap, long *hdr_len,
                        size_t *body_off, size_t *have_out) {
    size_t have = 0;
    for (;;) {
        if (have >= cap)
            return -1;
        ssize_t n = recv(fd, buf + have, cap - have, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return -2;
            return -3;
        }
        if (n == 0)
            return -3;
        have += (size_t)n;

        for (size_t i = 0; i + 3 < have; i++) {
            if (memcmp(buf + i, "\r\n\r\n", 4) == 0) {
                *hdr_len = (long)(i + 4);
                *body_off = i + 4;
                *have_out = have;
                return 0;
            }
        }
    }
}

typedef struct {
    char     method[8];
    char     path[300];
    char     query[QUERY_MAX];
    uint64_t content_length;
    int      expect_continue;
    int      chunked;
    int      have_x_size;
    uint64_t x_pkg_size;
    char     content_type[160];
} http_req_t;

static int parse_request(char *block, http_req_t *r) {
    memset(r, 0, sizeof(*r));

    char target[600];
    if (sscanf(block, "%7s %599s", r->method, target) != 2)
        return -1;

    char *q = strchr(target, '?');
    if (q) {
        *q = '\0';
        strncpy(r->query, q + 1, sizeof(r->query) - 1);
    }
    strncpy(r->path, target, sizeof(r->path) - 1);

    char *p = strchr(block, '\n');
    if (!p)
        return -1;
    p++;

    while (*p) {
        char *eol = strstr(p, "\r\n");
        if (!eol || eol == p)
            break;
        *eol = '\0';

        if (strncasecmp(p, "content-length:", 15) == 0) {
            r->content_length = strtoull(p + 15, NULL, 10);
        } else if (strncasecmp(p, "content-type:", 13) == 0) {
            const char *v = p + 13;
            while (*v == ' ' || *v == '\t')
                v++;
            strncpy(r->content_type, v, sizeof(r->content_type) - 1);
        } else if (strncasecmp(p, "x-pkg-size:", 11) == 0) {
            r->x_pkg_size = strtoull(p + 11, NULL, 10);
            r->have_x_size = 1;
        } else if (strncasecmp(p, "transfer-encoding:", 18) == 0) {
            const char *v = p + 18;
            while (*v == ' ' || *v == '\t')
                v++;
            
            for (const char *s = v; s[0]; s++) {
                if (strncasecmp(s, "chunked", 7) == 0) {
                    r->chunked = 1;
                    break;
                }
            }
        } else if (strncasecmp(p, "expect:", 7) == 0 &&
                   strstr(p, "100-continue")) {
            r->expect_continue = 1;
        }

        p = eol + 2;
    }
    return 0;
}


static int stream_body(int fd, const char *leftover, size_t leftover_len,
                       uint64_t content_length, int out_fd, uint64_t *received,
                       char *buf, size_t bufcap) {
    uint64_t total = 0;

    *received = 0;

    if (leftover_len > 0) {
        if ((uint64_t)leftover_len > content_length)
            leftover_len = (size_t)content_length;
        ssize_t wn = write(out_fd, leftover, leftover_len);
        if (wn != (ssize_t)leftover_len) {
            *received = total;
            return 3;
        }
        total += leftover_len;
        rx_bytes_add(leftover_len);
    }

    while (total < content_length) {
        uint64_t want = content_length - total;
        size_t chunk = want < bufcap ? (size_t)want : bufcap;
        ssize_t n = recv(fd, buf, chunk, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            *received = total;
            return (errno == EAGAIN || errno == EWOULDBLOCK) ? 1 : 2;
        }
        if (n == 0) {
            *received = total;
            return 2;
        }
        ssize_t wn = write(out_fd, buf, (size_t)n);
        if (wn != n) {
            *received = total;
            return 3;
        }
        total += (uint64_t)n;
        rx_bytes_add((uint64_t)n);
    }

    *received = total;
    return 0;
}


#define DRAIN_ALL UINT64_MAX

static void drain_request_body(int fd, uint64_t remaining) {
    char tmp[4096];
    while (remaining > 0) {
        size_t want =
            remaining < sizeof(tmp) ? (size_t)remaining : sizeof(tmp);
        ssize_t n = recv(fd, tmp, want, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            break;
        if (remaining != DRAIN_ALL)
            remaining -= (uint64_t)n;
    }
}




static int staged_head_sha256(const char *path, uint64_t maxbytes,
                              char out[65]) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    sha256_ctx c;
    uint8_t dg[32], buf[65536];
    sha256_init(&c);
    uint64_t left = maxbytes;
    size_t n;
    while (left > 0 &&
           (n = fread(buf, 1, left < sizeof(buf) ? (size_t)left
                                                  : sizeof(buf),
                       f)) > 0) {
        sha256_update(&c, buf, n);
        left -= n;
    }
    fclose(f);
    sha256_final(&c, dg);
    for (int i = 0; i < 32; ++i)
        sprintf(out + i * 2, "%02x", dg[i]);
    out[64] = '\0';
    return 0;
}


static void handle_staged_size(int fd, const char *query) {
    char name[NAME_MAX_LEN + 8];
    char body[256];
    if (!query_get(query, "name", name, sizeof(name)) ||
        sanitize_pkg_name(name, name, sizeof(name)) != 0) {
        send_json(fd, 400, "Bad Request",
                  "{\"ok\":false,\"error\":\"bad_name\"}");
        return;
    }
    char p[320];
    snprintf(p, sizeof(p), "%s/%s", TMP_DIR, name);
    struct stat st;
    if (stat(p, &st) != 0 || !S_ISREG(st.st_mode)) {
        send_json(fd, 404, "Not Found",
                  "{\"ok\":false,\"error\":\"not_found\"}");
        return;
    }
    snprintf(body, sizeof(body), "{\"ok\":true,\"size\":%lld}",
             (long long)st.st_size);
    send_json(fd, 200, "OK", body);
}

static void handle_ping(int fd) {
    char body[192];
    int busy = 0;
    if (pthread_mutex_trylock(&g_op_mtx) != 0)
        busy = 1;
    else
        pthread_mutex_unlock(&g_op_mtx);
    snprintf(body, sizeof(body),
             "{\"ok\":true,\"name\":\"pkg-server\",\"version\":\"%s\","
             "\"fw\":%d,\"port\":%d,\"busy\":%s}",
             VERSION, g_fw_major, SERVER_PORT, busy ? "true" : "false");
    send_json(fd, 200, "OK", body);
}

static int claim_stage_file(const char *desired, char *out, size_t outcap) {
    int fd = open(desired, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd >= 0) {
        snprintf(out, outcap, "%s", desired);
        return fd;
    }
    if (errno != EEXIST)
        return -1;

    char base[512];
    snprintf(base, sizeof(base), "%s", desired);
    size_t bl = strlen(base);
    if (bl > 4 && strcasecmp(base + bl - 4, ".pkg") == 0)
        bl -= 4;

    for (int n = 2; n < 100; n++) {
        snprintf(out, outcap, "%.*s_upd%d.pkg", (int)bl, base, n);
        fd = open(out, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd >= 0)
            return fd;
        if (errno != EEXIST)
            return -1;
    }
    snprintf(out, outcap, "%.*s_%ld_%d.pkg", (int)bl, base,
             (long)time(NULL), (int)getpid());
    fd = open(out, O_WRONLY | O_CREAT | O_EXCL, 0644);
    return fd;
}




static void install_staged(int fd, const char *stage_path, const char *name,
                           const char *query, uint64_t expect_total);

static void handle_chunk_install(int fd, const http_req_t *req,
                                 const char *leftover, size_t leftover_len) {
    char name[NAME_MAX_LEN + 8];
    char base_path[320];
    char stage_path[320] = "";
    char cid[CONTENTID_SIZE] = "";
    char body[2048];
    char esc_path[680];
    char esc_cid[CONTENTID_SIZE * 2];
    char *stream_buf = NULL;
    const char *via = "none";

    char vb[32];
    uint64_t off = 0, total = 0;
    int have_off = 0, have_total = 0, finalize = 0, bench = 0;

    if (query_get(req->query, "offset", vb, sizeof(vb))) {
        off = strtoull(vb, NULL, 10);
        have_off = 1;
    }
    if (query_get(req->query, "total", vb, sizeof(vb))) {
        total = strtoull(vb, NULL, 10);
        have_total = 1;
    }
    if (query_get(req->query, "finalize", vb, sizeof(vb)))
        finalize = (strcmp(vb, "1") == 0 || strcasecmp(vb, "true") == 0);
    if (query_get(req->query, "bench", vb, sizeof(vb)))
        bench = (strcmp(vb, "1") == 0 || strcasecmp(vb, "true") == 0);

    if (!have_off || !have_total || total == 0) {
        send_json(fd, 400, "Bad Request",
                  "{\"ok\":false,\"error\":\"need_offset_total\","
                  "\"detail\":\"chunk mode requires ?offset=&total=\"}");
        return;
    }

    if (off == 0 && !bench) {
        pkg_notify("notify.pkgnet.receiving");
        webui_set_state(WEBUI_UPLOADING);
    }

    if (!query_get(req->query, "name", name, sizeof(name)) ||
        sanitize_pkg_name(name, name, sizeof(name)) != 0) {
        snprintf(name, sizeof(name), "upload_%lld_%d.pkg",
                 (long long)time(NULL), (int)getpid());
    }
    snprintf(base_path, sizeof(base_path), "%s/%s", TMP_DIR, name);

    
    if (finalize && off == total && req->content_length == 0) {
        snprintf(stage_path, sizeof(stage_path), "%s/%s", TMP_DIR, name);
        install_staged(fd, stage_path, name, req->query, total);
        return;
    }

    if (req->chunked) {
        send_json(fd, 411, "Length Required",
                  "{\"ok\":false,\"error\":\"chunked_not_supported\"}");
        return;
    }
    if (req->content_length == 0) {
        send_json(fd, 400, "Bad Request",
                  "{\"ok\":false,\"error\":\"missing_body\",\"detail\":"
                  "\"range body is empty\"}");
        return;
    }

    stream_buf = (char *)malloc(BODY_CHUNK);
    if (!stream_buf) {
        send_json(fd, 500, "Internal Server Error",
                  "{\"ok\":false,\"error\":\"oom\"}");
        return;
    }

    if (bench) {
        if (off == 0)
            rx_bytes_reset();
        int bfd = open("/dev/null", O_WRONLY);
        if (bfd < 0) {
            send_json(fd, 500, "Internal Server Error",
                      "{\"ok\":false,\"error\":\"bench_open_failed\"}");
            free(stream_buf);
            return;
        }
        uint64_t received = 0;
        int sb = stream_body(fd, leftover, leftover_len,
                             req->content_length, bfd, &received,
                             stream_buf, BODY_CHUNK);
        close(bfd);
        free(stream_buf);
        if (sb != 0) {
            if (req->content_length > received)
                drain_request_body(fd, req->content_length - received);
            snprintf(body, sizeof(body),
                     "{\"ok\":false,\"error\":\"%s\",\"offset\":%llu,"
                     "\"written\":%llu}",
                     sb == 1 ? "upload_timeout" : (sb == 2 ? "unexpected_eof"
                                                            : "write_error"),
                     (unsigned long long)off,
                     (unsigned long long)received);
            send_json(fd, sb == 1 ? 408 : 400, "Bad Request", body);
            return;
        }
        snprintf(body, sizeof(body),
                 "{\"ok\":true,\"bench\":true,\"offset\":%llu,"
                 "\"written\":%llu}",
                 (unsigned long long)off,
                 (unsigned long long)(off + req->content_length));
        send_json(fd, 200, "OK", body);
        return;
    }

    int ffd = -1;
    if (off == 0) {
        
        g_webui_cur_total = total;
        snprintf(g_webui_cur_name, sizeof(g_webui_cur_name), "%s", name);
        char hb[80];
        struct stat stg_guard;
        if (stat(base_path, &stg_guard) == 0 &&
            S_ISREG(stg_guard.st_mode) && req->have_x_size &&
            req->x_pkg_size == (uint64_t)stg_guard.st_size &&
            query_get(req->query, "head256", hb, sizeof(hb))) {
            char dig[65];
            if (staged_head_sha256(base_path, HEAD_REUSE_BYTES, dig) == 0 &&
                strcasecmp(dig, hb) == 0) {
                snprintf(stage_path, sizeof(stage_path), "%s", base_path);
                log_line("REUSE(hash): staged pkg identical - skipping "
                         "upload");
                send_json(fd, 200, "OK",
                          "{\"ok\":true,\"reuse\":true,\"detail\":\"skip "
                          "to finalize\"}");
                free(stream_buf);
                return;
            }
        }

        
        struct stat stg;
        {
            int update = (stat(base_path, &stg) == 0 && S_ISREG(stg.st_mode) &&
                          req->have_x_size &&
                          req->x_pkg_size != (uint64_t)stg.st_size);
            if (update) {
                char nb[512];
                snprintf(nb, sizeof(nb), "%s", base_path);
                size_t bl = strlen(nb);
                if (bl > 4 && strcasecmp(nb + bl - 4, ".pkg") == 0)
                    bl -= 4;
                for (int n = 2; n < 100 && ffd < 0; n++) {
                    snprintf(stage_path, sizeof(stage_path), "%.*s_upd%d.pkg",
                             (int)bl, nb, n);
                    ffd = open(stage_path, O_WRONLY | O_CREAT | O_EXCL, 0644);
                    if (ffd < 0 && errno != EEXIST)
                        break;
                }
                if (ffd < 0) {
                    snprintf(stage_path, sizeof(stage_path), "%.*s_%ld_%d.pkg",
                             (int)bl, nb, (long)time(NULL), (int)getpid());
                    ffd = open(stage_path, O_WRONLY | O_CREAT | O_EXCL, 0644);
                }
            } else {
                snprintf(stage_path, sizeof(stage_path), "%s", base_path);
                ffd = open(stage_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            }
        }
        if (ffd < 0) {
            log_line("chunk open(%s) failed: %s", stage_path,
                     strerror(errno));
            send_json(fd, 500, "Internal Server Error",
                      "{\"ok\":false,\"error\":\"stage_open_failed\"}");
            free(stream_buf);
            return;
        }
        log_line("chunk session start: %s total=%llu", stage_path,
                 (unsigned long long)total);
        rx_bytes_reset();
    } else {
        snprintf(stage_path, sizeof(stage_path), "%s", base_path);
        ffd = open(stage_path, O_WRONLY, 0644);
        if (ffd < 0) {
            send_json(fd, 409, "Conflict",
                      "{\"ok\":false,\"error\":\"no_session\",\"detail\":"
                      "\"send the offset=0 chunk first\"}");
            free(stream_buf);
            return;
        }
    }

    if (lseek(ffd, (off_t)off, SEEK_SET) < 0) {
        close(ffd);
        send_json(fd, 500, "Internal Server Error",
                  "{\"ok\":false,\"error\":\"seek_failed\",\"detail\":"
                  "\"offset beyond staged size?\"}");
        free(stream_buf);
        return;
    }

    if (req->expect_continue) {
        static const char cont[] = "HTTP/1.1 100 Continue\r\n\r\n";
        send_all(fd, cont, sizeof(cont) - 1);
    }

    {
        uint64_t received = 0;
        int sb = stream_body(fd, leftover, leftover_len,
                             req->content_length, ffd, &received,
                             stream_buf, BODY_CHUNK);
        if (sb != 0) {
            close(ffd);
            log_line("chunk @%llu aborted at %llu/%llu (code %d) - "
                     "partial kept",
                     (unsigned long long)off,
                     (unsigned long long)received,
                     (unsigned long long)req->content_length, sb);
            if (req->content_length > received)
                drain_request_body(fd,
                                   req->content_length - received);
            snprintf(body, sizeof(body),
                     "{\"ok\":false,\"error\":\"%s\",\"offset\":%llu,"
                     "\"written\":%llu}",
                     sb == 1 ? "upload_timeout" : (sb == 2 ? "unexpected_eof"
                                                           : "disk_full"),
                     (unsigned long long)off,
                     (unsigned long long)received);
            if (sb == 1)
                send_json(fd, 408, "Request Timeout", body);
            else if (sb == 2)
                send_json(fd, 400, "Bad Request", body);
            else
                send_json(fd, 507, "Insufficient Storage", body);
            free(stream_buf);
            webui_set_state(WEBUI_IDLE);
            return;
        }
    }

    close(ffd);
    free(stream_buf);
    stream_buf = NULL;

    uint64_t written_end = off + req->content_length;

    
    if (off == 0 && req->content_length == total) {
        
        snprintf(g_webui_staged_name, sizeof(g_webui_staged_name), "%s", name);
        g_webui_staged_size = total;
        g_webui_staged = 1;
        webui_set_state(WEBUI_STAGED);
    } else {
        struct stat fs;
        if (stat(stage_path, &fs) == 0 &&
            (uint64_t)fs.st_size == total) {
            snprintf(g_webui_staged_name, sizeof(g_webui_staged_name), "%s", name);
            g_webui_staged_size = total;
            g_webui_staged = 1;
            webui_set_state(WEBUI_STAGED);
        }
    }

    if (!finalize) {
        snprintf(body, sizeof(body),
                 "{\"ok\":true,\"chunk\":true,\"offset\":%llu,"
                 "\"written\":%llu,\"next\":%llu}",
                 (unsigned long long)off, (unsigned long long)written_end,
                 (unsigned long long)written_end);
        send_json(fd, 200, "OK", body);
        return;
    }

    install_staged(fd, stage_path, name, req->query, total);
}

/* Shared finalize: verify the staged size, run sceAppInstUtil, register the
 * task and reply with the JSON the /install endpoint documents. Used by the
 * chunk endpoint and the web UI upload/install endpoints. */
static void install_staged(int fd, const char *stage_path, const char *name,
                            const char *query, uint64_t expect_total) {
    webui_set_state(WEBUI_INSTALLING);
    char body[2048];
    char esc_path[680];
    char esc_cid[CONTENTID_SIZE * 2];
    char cid[CONTENTID_SIZE] = "";
    const char *via = "none";

    struct stat asm_st;
    if (expect_total != 0 &&
        (stat(stage_path, &asm_st) != 0 ||
         (uint64_t)asm_st.st_size != expect_total)) {
        unsigned long long got =
            (stat(stage_path, &asm_st) == 0)
                ? (unsigned long long)asm_st.st_size : 0ull;
        snprintf(body, sizeof(body),
                 "{\"ok\":false,\"error\":\"size_gap\",\"expected\":%llu,"
                 "\"got\":%llu,\"detail\":\"ranges missing; re-send them "
                 "(partials are kept)\"}",
                 (unsigned long long)expect_total, got);
        send_json(fd, 400, "Bad Request", body);
        webui_set_state(WEBUI_ERROR);
        return;
    }

    pthread_mutex_lock(&g_op_mtx);
    int op_locked = 1;

    if (g_init_rc != 0 &&
        ps5_timed_init_phase(&g_init_state) == PS5_TIMED_INIT_RUNNING) {
        pthread_mutex_unlock(&g_op_mtx);
        op_locked = 0;
        send_json(fd, 503, "Service Unavailable",
                  "{\"ok\":false,\"error\":\"initializing\",\"detail\":"
                  "\"retry shortly\"}");
        return;
    }
    if (ensure_init_locked() != 0) {
        char err[32];
        hex32(err, sizeof(err), (unsigned)g_init_rc);
        pthread_mutex_unlock(&g_op_mtx);
        op_locked = 0;
        snprintf(body, sizeof(body),
                 "{\"ok\":false,\"installed\":false,"
                 "\"error\":\"init_failed\",\"code\":\"%s\"}", err);
        send_json(fd, 500, "Internal Server Error", body);
        return;
    }

    {
        int ctype = 0;
        char cbuf[16];
        if (query_get(query, "ctype", cbuf, sizeof(cbuf))) { int cv = atoi(cbuf); if (cv > 0) ctype = cv; }
        log_line("content_type: %d", ctype);
        int rc = do_install(stage_path, cid, sizeof(cid), &via, ctype);
        if (rc != 0) {
            char err[32];
            hex32(err, sizeof(err), (unsigned)rc);
            json_escape(stage_path, esc_path, sizeof(esc_path));
            snprintf(body, sizeof(body),
                     "{\"ok\":false,\"installed\":false,\"via\":\"%s\","
                     "\"error\":\"%s\",\"tmp_file\":\"%s\",\"staged\":"
                     "\"kept\",\"detail\":\"pkg kept for retry\"}",
                     via, err, esc_path);
            send_json(fd, 200, "OK", body);
            pkg_notify("notify.pkgnet.install_failed", err);
            webui_set_state(WEBUI_ERROR);
            goto out_unlocked;
        }
    }

    task_add(cid, stage_path);
    json_escape(cid, esc_cid, sizeof(esc_cid));
    json_escape(stage_path, esc_path, sizeof(esc_path));

    if (cid[0] == '\0') {
        snprintf(body, sizeof(body),
                 "{\"ok\":true,\"installed\":true,\"via\":\"%s\","
                 "\"content_id\":\"\",\"tmp_file\":\"%s\",\"staged\":"
                 "\"kept\",\"phase\":\"accepted\",\"note\":\"reinstall; "
                 "completion not trackable\"}",
                 via, esc_path);
        send_json(fd, 200, "OK", body);
        pkg_notify("notify.pkgnet.reinstall_accepted");
        webui_set_state(WEBUI_COMPLETE);
        goto out_unlocked;
    }

    int wait_s = 0;
    char wbuf[16];
    if (query_get(query, "wait", wbuf, sizeof(wbuf))) {
        int v = atoi(wbuf);
        if (v <= 0)
            wait_s = 0;
        else if (v > INSTALL_WAIT_MAX_S)
            wait_s = INSTALL_WAIT_MAX_S;
        else
            wait_s = v;
    }

    if (wait_s == 0) {
        snprintf(body, sizeof(body),
                 "{\"ok\":true,\"installed\":false,\"via\":\"%s\","
                 "\"content_id\":\"%s\",\"tmp_file\":\"%s\",\"staged\":"
                 "\"kept\",\"phase\":\"accepted\",\"note\":\"watch the SSE "
                 "stream /api/stream for phase=playable\"}",
                 via, esc_cid, esc_path);
        send_json(fd, 200, "OK", body);
        pkg_notify("notify.pkg.installing", name);
        goto out_unlocked;
    }

    appinst_status_t fin;
    memset(&fin, 0, sizeof(fin));
    int done = wait_terminal_locked(cid, wait_s, &fin);

    if (done && strncmp(fin.status, "playable", 8) == 0 &&
        fin.error_info.error_code == 0) {
        snprintf(body, sizeof(body),
                 "{\"ok\":true,\"installed\":true,\"via\":\"%s\","
                 "\"content_id\":\"%s\",\"phase\":\"%.15s\","
                 "\"downloaded_size\":%llu,\"total_size\":%llu,"
                 "\"error\":null,\"tmp_file\":\"%s\",\"staged\":\"kept\"}",
                 via, esc_cid, fin.status,
                 (unsigned long long)fin.downloaded_size,
                 (unsigned long long)fin.total_size, esc_path);
    } else if (done) {
        char err[32];
        hex32(err, sizeof(err), (unsigned)fin.error_info.error_code);
        snprintf(body, sizeof(body),
                 "{\"ok\":false,\"installed\":false,\"via\":\"%s\","
                 "\"content_id\":\"%s\",\"phase\":\"%.15s\","
                 "\"error\":\"%s\",\"tmp_file\":\"%s\",\"staged\":"
                 "\"kept\"}",
                 via, esc_cid, fin.status, err, esc_path);
    } else {
        snprintf(body, sizeof(body),
                 "{\"ok\":true,\"installed\":false,\"via\":\"%s\","
                 "\"content_id\":\"%s\",\"phase\":\"%.15s\","
                 "\"tmp_file\":\"%s\",\"staged\":\"kept\",\"note\":"
                 "\"watch /api/stream\"}",
                 via, esc_cid,
                 fin.status[0] ? fin.status : "wait", esc_path);
    }
    send_json(fd, 200, "OK", body);

out_unlocked:
    if (op_locked)
        pthread_mutex_unlock(&g_op_mtx);
}


/* ------------------------------------------------------------------------- */
/* Web UI endpoints (TCP 12800): single-file bundle + browser /api/* flow.  */
/* ------------------------------------------------------------------------- */

void pkg_server_set_webui_lang(const char *lang) {
    pthread_mutex_lock(&g_webui_lang_mtx);
    snprintf(g_webui_lang, sizeof(g_webui_lang), "%s",
             lang && lang[0] ? lang : "en");
    pthread_mutex_unlock(&g_webui_lang_mtx);
}

static void lan_ip(char *out, size_t cap) {
    out[0] = '\0';
    struct ifaddrs *ifa = NULL;
    if (getifaddrs(&ifa) != 0)
        return;
    for (struct ifaddrs *it = ifa; it; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET)
            continue;
        if (it->ifa_flags & IFF_LOOPBACK)
            continue;
        const uint32_t a =
            ntohl(((const struct sockaddr_in *)it->ifa_addr)->sin_addr.s_addr);
        if ((a & 0xFF000000u) == 0x7F000000u)
            continue;
        if ((a & 0xFFFF0000u) == 0xA9FE0000u)
            continue;
        snprintf(out, cap, "%u.%u.%u.%u", (a >> 24) & 0xFF, (a >> 16) & 0xFF,
                 (a >> 8) & 0xFF, a & 0xFF);
        break;
    }
    freeifaddrs(ifa);
}

static void webui_map_phase(const char *src, char *out, size_t cap) {
    if (!src || src[0] == '\0') {
        snprintf(out, cap, "accepted");
    } else if (strncmp(src, "playable", 8) == 0) {
        snprintf(out, cap, "complete");
    } else if (strncmp(src, "broken", 6) == 0 ||
               strncmp(src, "none", 4) == 0) {
        snprintf(out, cap, "error");
    } else if (strncmp(src, "transferring", 12) == 0) {
        /* Download/copy phase: report as installing so the web UI keeps
         * polling with a stable label. */
        snprintf(out, cap, "installing");
    } else {
        snprintf(out, cap, "%.30s", src);
    }
}

static void webui_send_index(int fd) {
    char hdr[192];
    const size_t index_len = webui_index_size();
    const int n = snprintf(hdr, sizeof(hdr),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html; charset=utf-8\r\n"
                           "Content-Length: %zu\r\n"
                           "Connection: close\r\n"
                           "\r\n",
                           index_len);
    if (n <= 0 || (size_t)n >= sizeof(hdr))
        return;
    send_all(fd, hdr, (size_t)n);
    send_all(fd, (const char *)webui_index_data(), index_len);
}

static void webui_status_payload(char *body, size_t len) {
    char ip[64];
    lan_ip(ip, sizeof(ip));

    int busy = 0;
    if (pthread_mutex_trylock(&g_op_mtx) != 0)
        busy = 1;
    else
        pthread_mutex_unlock(&g_op_mtx);

    char phase[32] = "idle";
    char err[40] = "";

    /* First, check our local webui state machine which tracks upload/install
     * independently of the Sony task system. This gives immediate feedback
     * during upload and early install before the task is created. */
    switch (g_webui_state) {
        case WEBUI_IDLE:
            snprintf(phase, sizeof(phase), "idle");
            break;
        case WEBUI_UPLOADING:
            snprintf(phase, sizeof(phase), "uploading");
            break;
        case WEBUI_STAGED:
            snprintf(phase, sizeof(phase), "staged");
            break;
        case WEBUI_INSTALLING:
            snprintf(phase, sizeof(phase), "installing");
            break;
        case WEBUI_COMPLETE:
            snprintf(phase, sizeof(phase), "complete");
            break;
        case WEBUI_ERROR:
            snprintf(phase, sizeof(phase), "error");
            break;
        default:
            snprintf(phase, sizeof(phase), "idle");
            break;
    }

    /* If we have a task CID, fall back to the Sony task system for more detail. */
    if (g_webui_last_cid[0] != '\0') {
        task_slot_t task;
        if (task_lookup(g_webui_last_cid, &task)) {
            /* Terminal results stay cached; in-flight snapshots expire after
             * STATUS_FRESH_MS so the poll keeps following Sony's live phase
             * instead of replaying the first "installing" forever. */
            if (task.terminal ||
                (task.snap_ms != 0 &&
                 mono_ms() - task.snap_ms < STATUS_FRESH_MS)) {
                webui_map_phase(task.phase, phase, sizeof(phase));
                if (task.error_code)
                    hex32(err, sizeof(err), task.error_code);
            } else {
                appinst_status_t st;
                memset(&st, 0, sizeof(st));
                pthread_mutex_lock(&g_op_mtx);
                task_slot_t fresh;
                const int still = task_lookup(g_webui_last_cid, &fresh);
                int rc = -1;
                if (still && !fresh.terminal && g_init_rc == 0) {
                    rc = sceAppInstUtilGetInstallStatus(g_webui_last_cid, &st);
                    if (rc == 0)
                        task_store_snapshot(g_webui_last_cid, &st);
                }
                pthread_mutex_unlock(&g_op_mtx);
                if (still && fresh.terminal) {
                    webui_map_phase(fresh.phase, phase, sizeof(phase));
                    if (fresh.error_code)
                        hex32(err, sizeof(err), fresh.error_code);
                } else if (rc == 0) {
                    webui_map_phase(st.status, phase, sizeof(phase));
                    if (st.error_info.error_code != 0)
                        hex32(err, sizeof(err),
                              (unsigned)st.error_info.error_code);
                } else {
                    webui_map_phase("", phase, sizeof(phase));
                }
            }
        }
    }

    /* Let a terminal result settle briefly so the browser's install wait can
     * see it, then snap back to idle instead of pinning phase "complete" and
     * "staged":true forever. */
    static uint64_t term_since = 0;
    const int is_terminal =
        strcmp(phase, "complete") == 0 || strcmp(phase, "error") == 0;
    if (is_terminal) {
        if (term_since == 0)
            term_since = mono_ms();
        if (mono_ms() - term_since > WEBUI_TERM_RESET_MS) {
            g_webui_state = WEBUI_IDLE;
            g_webui_last_cid[0] = '\0';
            g_webui_staged = 0;
            g_webui_staged_name[0] = '\0';
            g_webui_cur_name[0] = '\0';
            g_webui_cur_total = 0;
            snprintf(phase, sizeof(phase), "idle");
            term_since = 0;
        }
    } else {
        term_since = 0;
    }

    char esc_name[(NAME_MAX_LEN + 8) * 2 + 1];
    char esc_cid[CONTENTID_SIZE * 2 + 1];
    json_escape(g_webui_cur_name, esc_name, sizeof(esc_name));
    json_escape(g_webui_last_cid, esc_cid, sizeof(esc_cid));

    char lang[WEBUI_LANG_MAX];
    pthread_mutex_lock(&g_webui_lang_mtx);
    snprintf(lang, sizeof(lang), "%s", g_webui_lang);
    pthread_mutex_unlock(&g_webui_lang_mtx);

    snprintf(body, len,
             "{\"ok\":true,\"ip\":\"%s\",\"staged\":%s,\"language\":\"%s\","
             "\"phase\":\"%s\",\"error\":\"%s\",\"busy\":%s,\"name\":\"%s\","
             "\"total\":%llu,\"cid\":\"%s\"}",
             ip, g_webui_staged ? "true" : "false", lang, phase, err,
             busy ? "true" : "false", esc_name,
             (unsigned long long)g_webui_cur_total, esc_cid);
}

static void webui_handle_stream(int fd) {
    static const char headers[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n";
    send_all(fd, headers, sizeof(headers) - 1);

    char body[768];
    char line[864];
    for (;;) {
        if (g_shutdown)
            break;

        webui_status_payload(body, sizeof(body));
        const size_t blen = strlen(body);
        const int n = snprintf(line, sizeof(line), "data: %.*s,\"bytes\":%llu}\n\n",
                               (int)(blen - 1), body,
                               (unsigned long long)rx_bytes_get());
        if (n > 0 && (size_t)n < sizeof(line))
            send_all(fd, line, (size_t)n);

        /* Non-blocking peek: a closed tab reports EOF and tears us down. */
        char c;
        const ssize_t r = recv(fd, &c, 1, MSG_DONTWAIT);
        if (r == 0)
            break;
        if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
            errno != EINTR)
            break;

        usleep(250 * 1000);
    }
}

static void *webui_conn_handler(void *arg) {
    const int fd = (int)(long)arg;

    struct timeval tv = { .tv_sec = RECV_TIMEOUT_S, .tv_usec = 0 };
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buf[HDR_BUF_CAP];
    long hdr_len = 0;
    size_t body_off = 0, have = 0;
    const int rc = read_headers(fd, buf, sizeof(buf), &hdr_len, &body_off,
                                &have);
    if (rc != 0) {
        close(fd);
        return NULL;
    }
    buf[hdr_len] = '\0';

    http_req_t req;
    if (parse_request(buf, &req) != 0) {
        send_json(fd, 400, "Bad Request",
                  "{\"ok\":false,\"error\":\"bad_request\"}");
        close(fd);
        return NULL;
    }

    if (strcmp(req.path, "/") == 0 ||
        strcasecmp(req.path, "/index.html") == 0) {
        webui_send_index(fd);
    } else if (strcasecmp(req.path, "/api/stream") == 0) {
        webui_handle_stream(fd);
    } else {
        send_json(fd, 404, "Not Found",
                  "{\"ok\":false,\"error\":\"not_found\"}");
    }

    close(fd);
    return NULL;
}

static void *webui_accept_loop(void *arg) {
    const int srv = (int)(long)arg;
    while (!g_shutdown) {
        const int cfd = accept(srv, NULL, NULL);
        if (cfd < 0) {
            if (g_shutdown)
                break;
            if (errno == EINTR || errno == ECONNABORTED)
                continue;
            log_line("webui accept failed: %s", strerror(errno));
            break;
        }
        pthread_t tid;
        if (pthread_create(&tid, NULL, webui_conn_handler,
                           (void *)(long)cfd) != 0)
            close(cfd);
        else
            pthread_detach(tid);
    }
    close(srv);
    return NULL;
}

static void handle_install(int fd, const http_req_t *req,
                           const char *leftover, size_t leftover_len) {
    char name[NAME_MAX_LEN + 8];
    char base_path[320];
    char stage_path[320] = "";
    char cid[CONTENTID_SIZE] = "";
    char body[2048];
    char esc_path[680];
    char esc_cid[CONTENTID_SIZE * 2];
    const char *via = "none";
    char *stream_buf = NULL;
    int reuse_existing = 0;
    int op_locked = 0;

    
    pkg_notify("notify.pkgnet.receiving");
    if (req->chunked) {
        send_json(fd, 411, "Length Required",
                  "{\"ok\":false,\"error\":\"chunked_not_supported\","
                  "\"detail\":\"curl --data-binary sends Content-Length\"}");
        return;
    }
    if (req->content_length == 0) {
        send_json(fd, 400, "Bad Request",
                  "{\"ok\":false,\"error\":\"missing_body\",\"detail\":"
                  "\"POST the .pkg bytes as the request body\"}");
        return;
    }

    if (!query_get(req->query, "name", name, sizeof(name)) ||
        sanitize_pkg_name(name, name, sizeof(name)) != 0) {
        snprintf(name, sizeof(name), "upload_%lld_%d.pkg",
                 (long long)time(NULL), (int)getpid());
    }
    snprintf(base_path, sizeof(base_path), "%s/%s", TMP_DIR, name);

    
    struct stat stg;
    int update_conflict = 0;
    if (stat(base_path, &stg) == 0 && S_ISREG(stg.st_mode)) {
        if (req->have_x_size &&
            req->x_pkg_size != (uint64_t)stg.st_size) {
            update_conflict = 1;
            log_line("update detected: incoming %llu bytes vs staged %llu",
                     (unsigned long long)req->x_pkg_size,
                     (unsigned long long)stg.st_size);
        } else {
            reuse_existing = 1;
            snprintf(stage_path, sizeof(stage_path), "%s", base_path);
            log_line("REUSE: %llu-byte staged pkg exists - skipping "
                     "upload",
                     (unsigned long long)stg.st_size);
            pkg_notify("notify.pkgnet.reusing", name);
        }
    }

    
    if (reuse_existing && !req->expect_continue &&
        req->content_length > leftover_len)
        drain_request_body(fd, req->content_length - leftover_len);

    if (!reuse_existing) {
        stream_buf = (char *)malloc(BODY_CHUNK);
        if (!stream_buf) {
            send_json(fd, 500, "Internal Server Error",
                      "{\"ok\":false,\"error\":\"oom\"}");
            return;
        }

        if (req->expect_continue) {
            static const char cont[] = "HTTP/1.1 100 Continue\r\n\r\n";
            send_all(fd, cont, sizeof(cont) - 1);
        }

        int ffd = claim_stage_file(base_path, stage_path,
                                   sizeof(stage_path));
        if (ffd < 0) {
            log_line("claim_stage_file(%s) failed: %s", stage_path,
                     strerror(errno));
            send_json(fd, 500, "Internal Server Error",
                      "{\"ok\":false,\"error\":\"stage_open_failed\"}");
            free(stream_buf);
            return;
        }
        if (update_conflict)
            log_line("update staged as %s", stage_path);
        rx_bytes_reset();

        {
            uint64_t received = 0;
            int sb = stream_body(fd, leftover, leftover_len,
                                 req->content_length, ffd, &received,
                                 stream_buf, BODY_CHUNK);
            if (sb != 0) {
                close(ffd);
                unlink(stage_path);
                log_line("upload aborted at %llu/%llu bytes (code %d)",
                         (unsigned long long)received,
                         (unsigned long long)req->content_length, sb);
                if (sb == 1)
                    send_json(fd, 408, "Request Timeout",
                              "{\"ok\":false,\"error\":\"upload_timeout\"}");
                else if (sb == 2)
                    send_json(fd, 400, "Bad Request",
                              "{\"ok\":false,\"error\":\"unexpected_eof\"}");
                else
                    send_json(fd, 507, "Insufficient Storage",
                              "{\"ok\":false,\"error\":\"disk_full\"}");
                
                if (req->content_length > received)
                    drain_request_body(fd,
                                       req->content_length - received);
                free(stream_buf);
                return;
            }
        }

        close(ffd);
        log_line("staged %llu bytes -> %s",
                 (unsigned long long)req->content_length, stage_path);
        pkg_notify("notify.pkgnet.received", name);
    }
    free(stream_buf);
    stream_buf = NULL;

    
    if (pthread_mutex_trylock(&g_op_mtx) != 0) {
        send_json(fd, 503, "Service Unavailable",
                  "{\"ok\":false,\"error\":\"busy\",\"detail\":\"another "
                  "install is running; retry shortly\"}");
        return;
    }
    op_locked = 1;

    if (g_init_rc != 0) {
        if (ps5_timed_init_phase(&g_init_state) == PS5_TIMED_INIT_RUNNING) {
            send_json(fd, 503, "Service Unavailable",
                      "{\"ok\":false,\"error\":\"initializing\",\"detail\":"
                      "\"AppInstUtil Initialize still in flight; retry "
                      "shortly\"}");
            goto out_locked;
        }
        if (ensure_init_locked() != 0) {
            char err[32];
            hex32(err, sizeof(err), (unsigned)g_init_rc);
            snprintf(body, sizeof(body),
                     "{\"ok\":false,\"installed\":false,"
                     "\"error\":\"init_failed\",\"code\":\"%s\",\"detail\":"
                     "\"AppInstUtil not initialized - is the HEN/kstuff "
                     "loaded?\"}",
                     err);
            send_json(fd, 500, "Internal Server Error", body);
            goto out_locked;
        }
    }

    {
        int ctype = 0;
        char cbuf[16];
        if (query_get(req->query, "ctype", cbuf, sizeof(cbuf))) { int cv = atoi(cbuf); if (cv > 0) ctype = cv; }
        log_line("content_type: %d", ctype);
        int rc = do_install(stage_path, cid, sizeof(cid), &via, ctype);
        if (rc != 0) {
            char err[32];
            hex32(err, sizeof(err), (unsigned)rc);
            json_escape(stage_path, esc_path, sizeof(esc_path));
            snprintf(body, sizeof(body),
                     "{\"ok\":false,\"installed\":false,\"via\":\"%s\","
                     "\"error\":\"%s\",\"tmp_file\":\"%s\",\"staged\":"
                     "\"kept\",\"detail\":\"pkg kept for retry; remove "
                     "via FTP if unwanted\"}",
                     via, err, esc_path);
            send_json(fd, 200, "OK", body);
            pkg_notify("notify.pkgnet.install_failed", err);
            goto out_locked;
        }
    }

    task_add(cid, stage_path);
    json_escape(cid, esc_cid, sizeof(esc_cid));
    json_escape(stage_path, esc_path, sizeof(esc_path));

    if (cid[0] == '\0') {
        snprintf(body, sizeof(body),
                 "{\"ok\":true,\"installed\":true,\"via\":\"%s\","
                 "\"content_id\":\"\",\"tmp_file\":\"%s\","
                 "\"staged\":\"kept\",\"phase\":\"accepted\","
                 "\"note\":\"no content_id returned; completion not "
                 "trackable for this run\"}",
                 via, esc_path);
        send_json(fd, 200, "OK", body);
        pkg_notify("notify.pkgnet.reinstall_accepted");
        goto out_locked;
    }

    
    int wait_s = 0;
    char wbuf[16];
    if (query_get(req->query, "wait", wbuf, sizeof(wbuf))) {
        int v = atoi(wbuf);
        if (v <= 0)
            wait_s = 0;
        else if (v > INSTALL_WAIT_MAX_S)
            wait_s = INSTALL_WAIT_MAX_S;
        else
            wait_s = v;
    }

    if (wait_s == 0) {
        snprintf(body, sizeof(body),
                 "{\"ok\":true,\"installed\":false,\"via\":\"%s\","
                 "\"content_id\":\"%s\",\"tmp_file\":\"%s\","
                 "\"staged\":\"kept\",\"phase\":\"accepted\","
                 "\"note\":\"accepted by Sony; watch /api/stream until "
                 "phase=playable (completion toast fires too)\"}",
                 via, esc_cid, esc_path);
        send_json(fd, 200, "OK", body);
        pkg_notify("notify.pkg.installing", name);
        goto out_locked;
    }

    appinst_status_t fin;
    memset(&fin, 0, sizeof(fin));
    log_line("waiting for terminal state (%ds cap)", wait_s);
    int done = wait_terminal_locked(cid, wait_s, &fin);

    if (!done)
        pkg_notify("notify.pkgnet.still_installing");

    if (done) {
        int ok = (strncmp(fin.status, "playable", 8) == 0 &&
                  fin.error_info.error_code == 0);
        if (ok) {
            snprintf(body, sizeof(body),
                     "{\"ok\":true,\"installed\":true,\"via\":\"%s\","
                     "\"content_id\":\"%s\",\"phase\":\"%.15s\","
                     "\"downloaded_size\":%llu,\"total_size\":%llu,"
                     "\"error\":null,\"tmp_file\":\"%s\",\"staged\":"
                     "\"kept\"}",
                     via, esc_cid, fin.status,
                     (unsigned long long)fin.downloaded_size,
                     (unsigned long long)fin.total_size, esc_path);
        } else {
            char err[32];
            hex32(err, sizeof(err), (unsigned)fin.error_info.error_code);
            char esc_desc[640];
            json_escape(fin.error_info.description, esc_desc, sizeof(esc_desc));
            snprintf(body, sizeof(body),
                     "{\"ok\":false,\"installed\":false,\"via\":\"%s\","
                     "\"content_id\":\"%s\",\"phase\":\"%.15s\","
                     "\"downloaded_size\":%llu,\"total_size\":%llu,"
                     "\"error\":\"%s\",\"error_desc\":\"%s\","
                     "\"tmp_file\":\"%s\",\"staged\":\"kept\"}",
                     via, esc_cid, fin.status,
                     (unsigned long long)fin.downloaded_size,
                     (unsigned long long)fin.total_size, err,
                     esc_desc, esc_path);
        }
    } else {
        snprintf(body, sizeof(body),
                 "{\"ok\":true,\"installed\":false,\"via\":\"%s\","
                 "\"content_id\":\"%s\",\"phase\":\"%.15s\","
                 "\"tmp_file\":\"%s\",\"staged\":\"kept\","
                 "\"note\":\"terminal state not reached within %ds; keep "
                 "watching /api/stream\"}",
                 via, esc_cid,
                 fin.status[0] ? fin.status : "wait", esc_path, wait_s);
    }
    send_json(fd, 200, "OK", body);

out_locked:
    if (op_locked)
        pthread_mutex_unlock(&g_op_mtx);
}


static void *conn_handler(void *arg) {
    int fd = *(int *)arg;
    free(arg);
    pthread_detach(pthread_self());

    struct timeval tv;
    tv.tv_sec = RECV_TIMEOUT_S;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    int sockbuf = 4 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sockbuf, sizeof(sockbuf));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sockbuf, sizeof(sockbuf));

    char *raw = (char *)malloc(HDR_BUF_CAP);
    if (!raw) {
        close(fd);
        return NULL;
    }

    long hdr_len = 0;
    size_t body_off = 0;
    size_t have_total = 0;
    int rh = read_headers(fd, raw, HDR_BUF_CAP, &hdr_len, &body_off,
                          &have_total);
    if (rh == -1) {
        send_json(fd, 431, "Request Header Fields Too Large",
                  "{\"ok\":false,\"error\":\"headers_too_large\"}");
        free(raw);
        close(fd);
        return NULL;
    }
    if (rh == -2) {
        send_json(fd, 408, "Request Timeout",
                  "{\"ok\":false,\"error\":\"timeout\"}");
        free(raw);
        close(fd);
        return NULL;
    }
    if (rh != 0) {
        free(raw);
        close(fd);
        return NULL;
    }

    raw[hdr_len - 2] = '\0';

    http_req_t req;
    if (parse_request(raw, &req) != 0) {
        send_json(fd, 400, "Bad Request",
                  "{\"ok\":false,\"error\":\"bad_request\"}");
        free(raw);
        close(fd);
        return NULL;
    }

    log_line("%s %s%s%s (cl=%llu)", req.method, req.path,
             req.query[0] ? "?" : "", req.query,
             (unsigned long long)req.content_length);

    
    size_t lo = (have_total > body_off) ? (have_total - body_off) : 0;

    if (strcmp(req.path, "/ping") == 0 && strcmp(req.method, "GET") == 0) {
        handle_ping(fd);
    } else if (strcmp(req.path, "/install") == 0 &&
               (strcmp(req.method, "POST") == 0 ||
                strcmp(req.method, "PUT") == 0) &&
               (strstr(req.query, "total=") != NULL ||
                strstr(req.query, "offset=") != NULL)) {
        handle_chunk_install(fd, &req, raw + body_off, lo);
    } else if (strcmp(req.path, "/install") == 0 &&
               (strcmp(req.method, "POST") == 0 ||
                strcmp(req.method, "PUT") == 0)) {
        handle_install(fd, &req, raw + body_off, lo);
    } else if (strcmp(req.path, "/staged-size") == 0 &&
               strcmp(req.method, "GET") == 0) {
        handle_staged_size(fd, req.query);
    } else if (strcmp(req.path, "/shutdown") == 0 &&
               strcmp(req.method, "POST") == 0) {
        send_json(fd, 200, "OK",
                  "{\"ok\":true,\"bye\":true,\"detail\":\"shutting down for "
                  "a fresh injection\"}");
        log_line("/shutdown received - exiting");
        g_shutdown = 1;
        shutdown(g_srvfd, SHUT_RDWR);
    } else if (strcmp(req.path, "/ping") == 0 ||
               strcmp(req.path, "/install") == 0 ||
               strcmp(req.path, "/staged-size") == 0 ||
               strcmp(req.path, "/shutdown") == 0) {
        send_json(fd, 405, "Method Not Allowed",
                  "{\"ok\":false,\"error\":\"method_not_allowed\"}");
    } else {
        send_json(fd, 404, "Not Found",
                  "{\"ok\":false,\"error\":\"not_found\",\"routes\":"
                  "[\"GET /ping\",\"POST /install?name=...&wait=120\","
                  "\"GET /staged-size?name=x\",\"POST /shutdown\"]}");
    }

    free(raw);
    close(fd);
    return NULL;
}




static int port_is_listening(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return 0;
    struct timeval tv = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(SERVER_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int r = connect(fd, (struct sockaddr *)&a, sizeof(a));
    close(fd);
    return (r == 0);
}

static int probe_is_pkg_server(int *busy_out) {
    if (busy_out)
        *busy_out = 0;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return 0;
    struct timeval tv = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(SERVER_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        close(fd);
        return 0;
    }
    static const char req[] = "GET /ping HTTP/1.1\r\n"
                              "Host: ps5\r\n"
                              "Connection: close\r\n\r\n";
    if (send(fd, req, sizeof(req) - 1, 0) < 0) {
        close(fd);
        return 0;
    }
    char buf[512];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    close(fd);
    if (n <= 0)
        return 0;
    buf[n] = '\0';
    if (strstr(buf, "pkg-server") == NULL)
        return 0;
    if (busy_out && strstr(buf, "\"busy\":true") != NULL)
        *busy_out = 1;
    return 1;
}

static void post_shutdown_request(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return;
    struct timeval tv = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(SERVER_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) == 0) {
        static const char req[] = "POST /shutdown HTTP/1.1\r\n"
                                  "Host: ps5\r\n"
                                  "Content-Length: 0\r\n"
                                  "Connection: close\r\n\r\n";
        char junk[256];
        (void)!send(fd, req, sizeof(req) - 1, 0);
        (void)!recv(fd, junk, sizeof(junk), 0);
    }
    close(fd);
}

static void sweep_kill_named(const char *name) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t buf_size = 0;
    if (sysctl(mib, 4, NULL, &buf_size, NULL, 0) != 0 || buf_size == 0)
        return;

    uint8_t *buf = (uint8_t *)malloc(buf_size);
    if (!buf)
        return;
    if (sysctl(mib, 4, buf, &buf_size, NULL, 0) != 0) {
        free(buf);
        return;
    }

    pid_t me = getpid();
    for (uint8_t *ptr = buf; ptr < buf + buf_size;) {
        int ki_structsize = *(int *)ptr;
        if (ki_structsize <= 0 ||
            (size_t)(ptr - buf) + ki_structsize > buf_size ||
            ki_structsize <= KINFO_TDNAME_OFFSET)
            break;
        pid_t ki_pid = *(pid_t *)&ptr[KINFO_PID_OFFSET];
        const char *ki_tdname = (const char *)&ptr[KINFO_TDNAME_OFFSET];
        if (ki_pid != me && strcmp(ki_tdname, name) == 0) {
            log_line("takeover: SIGKILL %s pid=%d", name, (int)ki_pid);
            (void)kill(ki_pid, SIGKILL);
        }
        ptr += ki_structsize;
    }
    free(buf);
}

static void takeover_previous_instance(void) {
    int confirmed = 0;
    for (int i = 0; i < 3; i++) {
        int busy = 0;
        if (port_is_listening()) {
            if (probe_is_pkg_server(&busy)) {
                confirmed = 1;
                if (busy) {
                    log_line("takeover: old instance busy; waiting");
                    usleep(500000);
                }
                break;
            }
            log_line("takeover: :9090 owned by a non-pkg-server process; "
                     "leaving it alone");
            return;
        }
        /* Port free: nothing to replace. Return immediately so the serving
         * loop binds without stalling the UI toggle that started it. */
        return;
    }
    if (!confirmed)
        return;

    log_line("takeover: pkg-server detected on :9090 - replacing it");
    post_shutdown_request();

    
    for (int i = 0; i < TAKEOVER_BUSY_GRACE_S * 5; i++) {
        if (!port_is_listening())
            break;
        int busy = 0;
        probe_is_pkg_server(&busy);
        if (!busy)
            break;
        usleep(200000);
    }

    sweep_kill_named("pkg-server");

    for (int i = 0; i < 50 && port_is_listening(); i++)
        usleep(200000);

    
    if (port_is_listening() && probe_is_pkg_server(NULL)) {
        log_line("takeover: another fresh instance won the race - "
                 "backing off");
        pkg_notify("notify.pkgnet.takeover_backoff");
        g_shutdown_backoff = 1;
    } else {
        log_line("takeover: previous instance replaced");
    }
}




int pkg_server_main(void) {
    signal(SIGPIPE, SIG_IGN);

    if (mkdir("/user/data", 0777) != 0 && errno != EEXIST)
        log_line("mkdir /user/data: %s", strerror(errno));
    if (mkdir(TMP_DIR, 0777) != 0 && errno != EEXIST)
        log_line("mkdir %s: %s", TMP_DIR, strerror(errno));

    g_fw_major = ps5_detect_firmware_major();
    log_line("pkg-server %s starting (fw major %d)", VERSION, g_fw_major);

    
    takeover_previous_instance();
    if (g_shutdown_backoff)
        return 0;

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        pkg_notify("notify.pkgnet.socket_failed");
        return 1;
    }

    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    int sockbuf = 4 * 1024 * 1024;
    setsockopt(srv, SOL_SOCKET, SO_RCVBUF, &sockbuf, sizeof(sockbuf));
    setsockopt(srv, SOL_SOCKET, SO_SNDBUF, &sockbuf, sizeof(sockbuf));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SERVER_PORT);

    int bound = -1;
    for (int attempt = 0; attempt < 3 && bound != 0; attempt++) {
        bound = bind(srv, (struct sockaddr *)&addr, sizeof(addr));
        if (bound != 0) {
            log_line("bind :%d attempt %d: %s", SERVER_PORT, attempt + 1,
                     strerror(errno));
            sleep(2);
        }
    }
    if (bound != 0) {
        log_line("bind :%d failed: %s", SERVER_PORT, strerror(errno));
        pkg_notify("notify.pkgnet.bind_failed", SERVER_PORT,
                     strerror(errno));
        close(srv);
        return 1;
    }
    if (listen(srv, BACKLOG) != 0) {
        log_line("listen failed: %s", strerror(errno));
        close(srv);
        return 1;
    }
    log_line("listening on 0.0.0.0:%d", SERVER_PORT);
    pkg_notify("notify.pkgnet.listening", SERVER_PORT);
    /* Publish readiness only after the bind toast so the UI's "enabled"
     * notification always follows it. */
    g_srvfd = srv;

    /* Web UI listener (TCP 12800): single-file bundle + /api proxy. Best
     * effort - the DPI API keeps serving even if this bind fails. */
    int wsrv = socket(AF_INET, SOCK_STREAM, 0);
    if (wsrv < 0) {
        log_line("webui socket failed: %s", strerror(errno));
    } else {
        int wone = 1;
        setsockopt(wsrv, SOL_SOCKET, SO_REUSEADDR, &wone, sizeof(wone));
        struct sockaddr_in waddr;
        memset(&waddr, 0, sizeof(waddr));
        waddr.sin_family = AF_INET;
        waddr.sin_addr.s_addr = htonl(INADDR_ANY);
        waddr.sin_port = htons(WEBUI_PORT);
        int wok = 1;
        if (bind(wsrv, (struct sockaddr *)&waddr, sizeof(waddr)) != 0) {
            log_line("webui bind :%d failed: %s", WEBUI_PORT,
                     strerror(errno));
            wok = 0;
        }
        if (wok && listen(wsrv, BACKLOG) != 0) {
            log_line("webui listen failed: %s", strerror(errno));
            wok = 0;
        }
        if (!wok) {
            close(wsrv);
        } else {
            g_webui_fd = wsrv;
            log_line("webui listening on 0.0.0.0:%d", WEBUI_PORT);
            pkg_notify("notify.pkgnet.webui_listening", WEBUI_PORT);
            if (pthread_create(&g_webui_thread, NULL, webui_accept_loop,
                               (void *)(long)wsrv) == 0) {
                g_webui_thread_created = 1;
            } else {
                log_line("webui thread create failed");
                close(wsrv);
                g_webui_fd = -1;
            }
        }
    }

    pid_t me = getpid();
    if (jb_escalate_pid(me) == 0) {
        log_line("escalated: root + sandbox escape + SYSTEM_AUTHID (pid %d)",
                 (int)me);
    } else {
        pkg_notify("notify.pkgnet.escalation_failed");
        log_line("FATAL: ucred escalation failed");
    }

    pthread_mutex_lock(&g_op_mtx);
    (void)ensure_init_locked();
    pthread_mutex_unlock(&g_op_mtx);

    while (!g_shutdown) {
        int cfd = accept(srv, NULL, NULL);
        if (cfd < 0) {
            if (g_shutdown)
                break;
            if (errno == EINTR || errno == ECONNABORTED)
                continue;
            log_line("accept failed: %s", strerror(errno));
            continue;
        }

        int *arg = (int *)malloc(sizeof(int));
        if (!arg) {
            close(cfd);
            continue;
        }
        *arg = cfd;

        pthread_t tid;
        if (pthread_create(&tid, NULL, conn_handler, arg) != 0) {
            free(arg);
            close(cfd);
        }
    }

    log_line("shutting down cleanly (takeover or /shutdown)");
    close(srv);

    if (g_webui_thread_created) {
        const int wfd = g_webui_fd;
        if (wfd >= 0)
            shutdown(wfd, SHUT_RDWR);
        pthread_join(g_webui_thread, NULL);
        g_webui_thread_created = 0;
        g_webui_fd = -1;
    }
    return 0;
}

/* Facade start: reset the one-shot shutdown/backoff state so the worker can
 * serve again after a previous session. */
void pkg_server_prepare(void) {
    g_shutdown = 0;
    g_shutdown_backoff = 0;
    g_srvfd = -1;
    g_webui_fd = -1;
}

/* Facade stop: mirror the /shutdown handler (flag + wake the accept loops). */
void pkg_server_request_stop(void) {
    g_shutdown = 1;
    if (g_srvfd >= 0)
        shutdown(g_srvfd, SHUT_RDWR);
    const int wfd = g_webui_fd;
    if (wfd >= 0)
        shutdown(wfd, SHUT_RDWR);
}

/* Facade liveness: bound, listening, and not stopping. */
int pkg_server_is_listening(void) {
    return g_srvfd >= 0 && !g_shutdown;
}
