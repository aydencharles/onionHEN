#ifndef PS5UPLOAD_AUTHID_H
#define PS5UPLOAD_AUTHID_H

#include <stddef.h>
#include <sys/sysctl.h>
#include <string.h>

#define PS5_SYSTEM_INSTALL_AUTHID 0x4801000000000013ULL

_Static_assert(PS5_SYSTEM_INSTALL_AUTHID == 0x4801000000000013ULL,
               "PS5_SYSTEM_INSTALL_AUTHID value mismatch");

#define PS5_JB_AUTHID PS5_SYSTEM_INSTALL_AUTHID

static inline int ps5_parse_firmware_major(const char *kv) {
    if (!kv) return 0;
    const char *p = strstr(kv, "releases/");
    if (p) {
        p += strlen("releases/");
        int major = 0;
        int consumed = 0;
        while (*p >= '0' && *p <= '9') {
            major = major * 10 + (*p - '0');
            consumed++;
            p++;
        }
        if (consumed > 0) return major;
    }
    const char *scan = kv;
    if (strncmp(scan, "FreeBSD 11.0", 12) == 0) scan += 12;
    while (*scan) {
        if (scan[0] >= '0' && scan[0] <= '9') {
            int major = 0;
            int consumed = 0;
            const char *q = scan;
            while (*q >= '0' && *q <= '9') {
                major = major * 10 + (*q - '0');
                consumed++;
                q++;
            }
            if (consumed > 0 && *q == '.' &&
                q[1] >= '0' && q[1] <= '9' && q[2] >= '0' && q[2] <= '9' &&
                (q[3] < '0' || q[3] > '9')) {
                return major;
            }
            scan = q;
        }
        scan++;
    }
    return 0;
}

static inline int ps5_detect_firmware_major(void) {
    char buf[256];
    size_t sz = sizeof(buf);
    if (sysctlbyname("kern.version", buf, &sz, NULL, 0) != 0)
        return 0;
    buf[sizeof(buf) - 1] = '\0';
    return ps5_parse_firmware_major(buf);
}

#endif
