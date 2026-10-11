#include "workspace.h"
#include <string.h>
#include <errno.h>
#include <glib/gstdio.h>

char *workspace_note_label(const char *name) {
    const char *dot = strrchr(name,'.');
    return dot && (!strcmp(dot,".md") || !strcmp(dot,".markdown") || !strcmp(dot,".txt"))
        ? g_strndup(name,(gsize)(dot-name)) : g_strdup(name);
}

static gint order(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer data) {
    (void)data; gboolean ad, bd; char *an, *bn;
    gtk_tree_model_get(model,a,WS_DIRECTORY,&ad,WS_NAME,&an,-1);
    gtk_tree_model_get(model,b,WS_DIRECTORY,&bd,WS_NAME,&bn,-1);
    int result = ad != bd ? (ad ? -1 : 1) : g_utf8_collate(an ? an : "", bn ? bn : "");
    g_free(an); g_free(bn); return result;
}

GtkTreeStore *workspace_store(void) {
    GtkTreeStore *store = gtk_tree_store_new(WS_COLUMNS,G_TYPE_STRING,G_TYPE_FILE,G_TYPE_BOOLEAN,G_TYPE_BOOLEAN);
    gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(store),order,NULL,NULL);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store),GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,GTK_SORT_ASCENDING);
    return store;
}

gboolean workspace_populate(GtkTreeStore *store, GtkTreeIter *parent, GFile *folder, GError **error) {
    GFileEnumerator *items = g_file_enumerate_children(folder,
        "standard::name,standard::display-name,standard::type,standard::is-hidden,standard::is-symlink",
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,error);
    if (!items) return FALSE;
    /* Collect first: an enumeration failure must not leave a partial tree. */
    GPtrArray *infos = g_ptr_array_new_with_free_func(g_object_unref);
    GError *local = NULL; GFileInfo *info;
    while ((info = g_file_enumerator_next_file(items,NULL,&local))) g_ptr_array_add(infos,info);
    g_object_unref(items);
    if (local) { g_propagate_error(error,local); g_ptr_array_unref(infos); return FALSE; }
    GtkTreeIter child;
    while (gtk_tree_model_iter_children(GTK_TREE_MODEL(store),&child,parent)) gtk_tree_store_remove(store,&child);
    for (guint i = 0; i < infos->len; i++) {
        info = g_ptr_array_index(infos,i);
        if (g_file_info_get_is_hidden(info) || g_file_info_get_is_symlink(info)) continue;
        GFileType type = g_file_info_get_file_type(info);
        const char *name = g_file_info_get_name(info);
        gboolean directory = type == G_FILE_TYPE_DIRECTORY;
        if (!directory && (type != G_FILE_TYPE_REGULAR || !(g_str_has_suffix(name,".md") || g_str_has_suffix(name,".markdown") || g_str_has_suffix(name,".txt")))) continue;
        GFile *file = g_file_get_child(folder,name); GtkTreeIter row;
        gtk_tree_store_append(store,&row,parent);
        char *label = directory ? g_strdup(g_file_info_get_display_name(info)) : workspace_note_label(g_file_info_get_display_name(info));
        gtk_tree_store_set(store,&row,WS_NAME,label,WS_FILE,file,WS_DIRECTORY,directory,WS_LOADED,!directory,-1);
        g_free(label);
        if (directory) { GtkTreeIter placeholder; gtk_tree_store_append(store,&placeholder,&row); gtk_tree_store_set(store,&placeholder,WS_NAME,"",-1); }
        g_object_unref(file);
    }
    /* Retain an expander for empty folders so they can be collapsed again. */
    if (parent && !gtk_tree_model_iter_has_child(GTK_TREE_MODEL(store),parent)) {
        GtkTreeIter empty; gtk_tree_store_append(store,&empty,parent);
        gtk_tree_store_set(store,&empty,WS_NAME,"(empty)",WS_LOADED,TRUE,-1);
    }
    if (parent) gtk_tree_store_set(store,parent,WS_LOADED,TRUE,-1);
    g_ptr_array_unref(infos); return TRUE;
}

gboolean workspace_root(GtkTreeStore *store, GFile *folder, GError **error) {
    /* Prepare the new root before dropping the previous workspace. */
    GtkTreeStore *next = workspace_store();
    gboolean ok = workspace_populate(next,NULL,folder,error);
    if (ok) {
        gtk_tree_store_clear(store);
        GtkTreeIter source;
        if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(next),&source)) do {
            char *name; GFile *file; gboolean directory,loaded; GtkTreeIter row;
            gtk_tree_model_get(GTK_TREE_MODEL(next),&source,WS_NAME,&name,WS_FILE,&file,WS_DIRECTORY,&directory,WS_LOADED,&loaded,-1);
            gtk_tree_store_append(store,&row,NULL);
            gtk_tree_store_set(store,&row,WS_NAME,name,WS_FILE,file,WS_DIRECTORY,directory,WS_LOADED,loaded,-1);
            if (directory) { GtkTreeIter child; gtk_tree_store_append(store,&child,&row); gtk_tree_store_set(store,&child,WS_NAME,"",-1); }
            g_free(name); g_object_unref(file);
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(next),&source));
    }
    g_object_unref(next); return ok;
}

static char **path_parts(const char *name, GError **error) {
    if (!name || !*name || !g_utf8_validate(name,-1,NULL)) {
        g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_FILENAME,"Use a visible relative file or folder path."); return NULL;
    }
    char **parts = g_strsplit(name,"/",-1); guint count = g_strv_length(parts);
    for (guint i = 0; i < count; i++) {
        if (!*parts[i] || parts[i][0] == '.') {
            g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_FILENAME,"Use a relative path without empty, hidden, or parent components.");
            g_strfreev(parts); return NULL;
        }
    }
    return parts;
}

gboolean workspace_missing_parent(GFile *parent, const char *name, char **missing, GError **error) {
    *missing = NULL; char **parts = path_parts(name,error); if (!parts) return FALSE;
    guint count = g_strv_length(parts); GFile *folder = g_object_ref(parent); gboolean ok = TRUE;
    for (guint i = 0; i+1 < count; i++) {
        GFile *next = g_file_get_child(folder,parts[i]); GError *local = NULL;
        GFileInfo *info = g_file_query_info(next,"standard::type,standard::is-symlink",G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,&local);
        g_object_unref(folder); folder = next;
        if (!info) {
            if (g_error_matches(local,G_IO_ERROR,G_IO_ERROR_NOT_FOUND)) {
                g_clear_error(&local); g_free(parts[count-1]); parts[count-1] = NULL;
                *missing = g_strjoinv("/",parts);
            } else { g_propagate_error(error,local); ok = FALSE; }
            break;
        }
        gboolean valid = !g_file_info_get_is_symlink(info) && g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
        g_object_unref(info);
        if (!valid) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_DIRECTORY,"A path component is not a regular folder."); ok = FALSE; break; }
    }
    g_object_unref(folder); g_strfreev(parts); return ok;
}

gboolean workspace_create(GFile *parent, const char *name, gboolean directory, GFile **created, GError **error) {
    *created = NULL;
    char **parts = path_parts(name,error); if (!parts) return FALSE;
    guint count = g_strv_length(parts);
    GFile *folder = g_object_ref(parent);
    for (guint i = 0; i+1 < count; i++) {
        GFile *next = g_file_get_child(folder,parts[i]); GError *local = NULL;
        if (!g_file_make_directory(next,NULL,&local)) {
            if (!g_error_matches(local,G_IO_ERROR,G_IO_ERROR_EXISTS)) {
                g_propagate_error(error,local); g_object_unref(next); g_object_unref(folder); g_strfreev(parts); return FALSE;
            }
            g_clear_error(&local);
        }
        GFileInfo *info = g_file_query_info(next,"standard::type,standard::is-symlink",G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,error);
        gboolean valid = info && !g_file_info_get_is_symlink(info) && g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
        if (info && !valid) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_DIRECTORY,"A path component is not a regular folder.");
        g_clear_object(&info); g_object_unref(folder); folder = next;
        if (!valid) { g_object_unref(folder); g_strfreev(parts); return FALSE; }
    }
    const char *base = parts[count-1];
    char *filename = !directory && !(g_str_has_suffix(base,".md") || g_str_has_suffix(base,".markdown") || g_str_has_suffix(base,".txt")) ? g_strconcat(base,".md",NULL) : g_strdup(base);
    GFile *file = g_file_get_child(folder,filename);
    g_free(filename); g_strfreev(parts); g_object_unref(folder);
    gboolean ok;
    if (directory) ok = g_file_make_directory(file,NULL,error);
    else {
        GFileOutputStream *stream = g_file_create(file,G_FILE_CREATE_NONE,NULL,error);
        ok = stream && g_output_stream_close(G_OUTPUT_STREAM(stream),NULL,error);
        g_clear_object(&stream);
    }
    if (ok) *created = file; else g_object_unref(file);
    return ok;
}

gboolean workspace_delete(GFile *file, GError **error) {
    GFileInfo *info = g_file_query_info(file,"standard::type,standard::is-symlink",
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,error);
    if (!info) return FALSE;
    gboolean directory = !g_file_info_get_is_symlink(info) && g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
    g_object_unref(info);
    if (directory) {
        GFileEnumerator *items = g_file_enumerate_children(file,"standard::name",G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,error);
        if (!items) return FALSE;
        gboolean ok = TRUE; GError *local = NULL;
        while ((info = g_file_enumerator_next_file(items,NULL,&local))) {
            GFile *child = g_file_get_child(file,g_file_info_get_name(info)); g_object_unref(info);
            ok = workspace_delete(child,error); g_object_unref(child);
            if (!ok) break;
        }
        g_object_unref(items);
        if (local) { g_propagate_error(error,local); return FALSE; }
        if (!ok) return FALSE;
    }
    return g_file_delete(file,NULL,error);
}

/* Walk in the same sorted, depth-first order as the tree, including collapsed folders. */
static gboolean collect_notes(GFile *folder, GPtrArray *notes, GError **error) {
    GtkTreeStore *store = workspace_store();
    if (!workspace_populate(store,NULL,folder,error)) { g_object_unref(store); return FALSE; }
    GtkTreeIter it; gboolean ok = TRUE;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&it)) do {
        GFile *file; gboolean directory;
        gtk_tree_model_get(GTK_TREE_MODEL(store),&it,WS_FILE,&file,WS_DIRECTORY,&directory,-1);
        if (directory) { ok = collect_notes(file,notes,error); g_object_unref(file); }
        else g_ptr_array_add(notes,file);
        if (!ok) break;
    } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(store),&it));
    g_object_unref(store); return ok;
}

GPtrArray *workspace_notes(GFile *folder, GError **error) {
    GPtrArray *notes = g_ptr_array_new_with_free_func(g_object_unref);
    if (!collect_notes(folder,notes,error)) { g_ptr_array_unref(notes); return NULL; }
    return notes;
}

GFile *workspace_neighbor(GPtrArray *notes, GFile *current, gboolean next) {
    if (!notes->len) return NULL;
    guint index = next ? 0 : notes->len-1;
    for (guint i = 0; current && i < notes->len; i++) if (g_file_equal(current,g_ptr_array_index(notes,i))) {
        index = next ? (i+1)%notes->len : (i+notes->len-1)%notes->len; break;
    }
    return g_object_ref(g_ptr_array_index(notes,index));
}

gboolean workspace_seed(GFile *folder, GFile **created, GError **error) {
    *created = NULL;
    GPtrArray *notes = workspace_notes(folder,error);
    if (!notes) return FALSE;
    gboolean empty = notes->len == 0; g_ptr_array_unref(notes);
    if (!empty) return TRUE;
    return workspace_create(folder,"undefined.md",FALSE,created,error);
}

gboolean workspace_write_state(const char *path, GFile *folder, GError **error) {
    char *dir = g_path_get_dirname(path);
    if (g_mkdir_with_parents(dir,0700) != 0) {
        int code = errno;
        g_set_error(error,G_FILE_ERROR,g_file_error_from_errno(code),"Cannot remember workspace: %s",g_strerror(code));
        g_free(dir); return FALSE;
    }
    g_free(dir);
    char *uri = g_file_get_uri(folder);
    gboolean ok = g_file_set_contents(path,uri,-1,error); g_free(uri); return ok;
}

GFile *workspace_read_state(const char *path, GError **error) {
    char *value = NULL;
    if (!g_file_get_contents(path,&value,NULL,error)) return NULL;
    g_strstrip(value);
    if (!*value) {
        g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"No remembered workspace."); g_free(value); return NULL;
    }
    /* Accept the stored URI and native paths from older/manual state files. */
    GFile *folder = g_file_new_for_commandline_arg(value); g_free(value);
    GFileInfo *info = g_file_query_info(folder,"standard::type",G_FILE_QUERY_INFO_NONE,NULL,error);
    if (!info) { g_object_unref(folder); return NULL; }
    gboolean directory = g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY; g_object_unref(info);
    if (!directory) {
        g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_DIRECTORY,"Remembered workspace is not a folder."); g_object_unref(folder); return NULL;
    }
    return folder;
}
