#ifndef SM_MANUAL_H
#define SM_MANUAL_H

#include <stdbool.h>

struct AppDbTitleList;

// Visit each manual source path from manual.lst.
bool sm_manual_for_each_path(bool (*visit)(const char *path, void *ctx),
                             void *ctx);
// Mark previously installed manual titles as deleted when they disappear from
// app.db, and remove their source lines from manual.lst.
bool sm_manual_reconcile_deleted_titles(const struct AppDbTitleList *app_db_titles,
                                        bool app_db_titles_ready);
// Record that a manual source is installed and associated with a title.
void sm_manual_note_installed(const char *manual_source_path,
                              const char *title_id,
                              const char *title_name);

#endif
