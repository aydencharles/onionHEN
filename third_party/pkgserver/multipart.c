/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Minimal multipart/form-data header parsing (host-clean).
 */
#include "multipart.h"

#include <ctype.h>
#include <string.h>

int pkgserver_multipart_boundary(const char *content_type, char *out,
                                 size_t cap) {
    if (!content_type || !out || cap == 0)
        return -1;

    const char *p = content_type;
    while ((p = strstr(p, "boundary")) != NULL) {
        const char *after = p + 8;
        while (*after == '=' || *after == ' ' || *after == '\t')
            after++;
        if (*after == '\0' || *after == ';')
            return -1;

        const char *start = after;
        const char *end = after;
        if (*start == '"') {
            start++;
            end = start;
            while (*end && *end != '"')
                end++;
        } else {
            while (*end && *end != ';' && *end != ' ' && *end != '\t')
                end++;
        }
        const size_t len = (size_t)(end - start);
        if (len == 0 || len >= cap)
            return -1;
        memcpy(out, start, len);
        out[len] = '\0';
        return 0;
    }
    return -1;
}

int pkgserver_multipart_filename(const char *disposition, char *out,
                                 size_t cap) {
    if (!disposition || !out || cap == 0)
        return -1;

    const char *p = disposition;
    while ((p = strstr(p, "filename")) != NULL) {
        const char *after = p + 8;
        while (*after == '=' || *after == ' ' || *after == '\t')
            after++;
        if (*after == '\0')
            return -1;

        const char *start = after;
        const char *end = after;
        if (*start == '"') {
            start++;
            end = start;
            while (*end && *end != '"')
                end++;
        } else {
            while (*end && *end != ';' && *end != ' ' && *end != '\t')
                end++;
        }
        const size_t len = (size_t)(end - start);
        if (len == 0 || len >= cap)
            return -1;
        memcpy(out, start, len);
        out[len] = '\0';
        return 0;
    }
    return -1;
}
