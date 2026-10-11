#ifndef WRITR_WORKSPACE_H
#define WRITR_WORKSPACE_H

#include <gtk/gtk.h>

enum { WS_NAME, WS_FILE, WS_DIRECTORY, WS_LOADED, WS_COLUMNS };

GtkTreeStore *workspace_store(void);

char *workspace_note_label(const char *name);

gboolean workspace_populate(GtkTreeStore *store, GtkTreeIter *parent, GFile *folder, GError **error);

gboolean workspace_root(GtkTreeStore *store, GFile *folder, GError **error);

gboolean workspace_create(GFile *parent, const char *name, gboolean directory, GFile **created, GError **error);

gboolean workspace_missing_parent(GFile *parent, const char *name, char **missing, GError **error);

gboolean workspace_delete(GFile *file, GError **error);

GPtrArray *workspace_notes(GFile *folder, GError **error);

GFile *workspace_neighbor(GPtrArray *notes, GFile *current, gboolean next);

gboolean workspace_seed(GFile *folder, GFile **created, GError **error);

gboolean workspace_write_state(const char *path, GFile *folder, GError **error);

GFile *workspace_read_state(const char *path, GError **error);

#endif
