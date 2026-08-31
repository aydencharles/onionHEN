#ifndef PS5UPLOAD_DPI_SCE_APP_INST_UTIL_H
#define PS5UPLOAD_DPI_SCE_APP_INST_UTIL_H

#include <stdint.h>

#define PLAYGOSCENARIOID_SIZE 3
#define CONTENTID_SIZE        0x30
#define LANGUAGE_SIZE         8

#define SCE_NUM_LANGUAGES 30
#define SCE_NUM_IDS       64

typedef char playgo_scenario_id_t[PLAYGOSCENARIOID_SIZE];
typedef char language_t[LANGUAGE_SIZE];
typedef char content_id_t[CONTENTID_SIZE];

typedef struct {
    content_id_t content_id;
    int          content_type;
    int          content_platform;
} SceAppInstallPkgInfo;

typedef struct {
    const char *uri;
    const char *ex_uri;
    const char *playgo_scenario_id;
    const char *content_id;
    const char *content_name;
    const char *icon_url;
} MetaInfo;

typedef struct {
    language_t           languages[SCE_NUM_LANGUAGES];
    playgo_scenario_id_t playgo_scenario_ids[SCE_NUM_IDS];
    content_id_t         content_ids[SCE_NUM_IDS];
    unsigned char        unknown[6480];
} PlayGoInfo;

#include <assert.h>
_Static_assert(sizeof(SceAppInstallPkgInfo) == CONTENTID_SIZE + 8,
               "SceAppInstUtil PkgInfo size mismatch");
_Static_assert(sizeof(PlayGoInfo) == 9984,
               "PlayGoInfo size mismatch (expected 9984)");
_Static_assert(sizeof(MetaInfo) == 6 * sizeof(void*),
               "MetaInfo ABI size mismatch");

extern int sceAppInstUtilInitialize(void);

extern int sceAppInstUtilInstallByPackage(MetaInfo *meta,
                                          SceAppInstallPkgInfo *pkg_info,
                                          PlayGoInfo *playgo);

#endif
