#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <glib/gstdio.h>
#include "gap.h"
#include "markdown.h"
#include "workspace.h"
#include "paste.h"
#include "images.h"

typedef enum { INPUT_NONE, INPUT_WORKSPACE, INPUT_NOTE, INPUT_FOLDER } InputMode;

typedef struct { size_t at; char *text; size_t n; gboolean insert; guint group; } Edit;

typedef struct {
    GtkWidget *window, *view, *sidebar, *tree, *input;
    InputMode input_mode; GFile *input_parent;
    GHashTable *drafts; gboolean auto_name; GtkTextBuffer *buffer;
    GtkTreeStore *store; GFile *workspace; gboolean source; ImageLayer *images;
    Gap gap; GFile *file; char *etag, *saved;
    GPtrArray *history; guint position, group, depth, style_source;
    gboolean replay, loading, closed; guint document;
} Editor;

static void fail(Editor *e, const char *message) {
    if (!e->window) { g_printerr("%s\n", message); return; }
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(e->window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", message);
    gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d);
}

static gboolean dirty(Editor *e) {
    char *s = gap_text(&e->gap); gboolean result = !s || strcmp(s, e->saved ? e->saved : ""); free(s); return result;
}

static void title(Editor *e) {
    if (!e->window) return;
    char *name = e->file ? g_file_get_basename(e->file) : g_strdup("Untitled");
    char *s = g_strdup_printf("%s%s — Writr", dirty(e) ? "• " : "", name);
    gtk_window_set_title(GTK_WINDOW(e->window), s); g_free(s); g_free(name);
}

static void edit_free(gpointer p) { Edit *edit = p; g_free(edit->text); g_free(edit); }

static size_t byte_offset(Editor *e, GtkTextIter *it) {
    /* GTK positions are Unicode characters; the gap stores exact UTF-8 bytes. */
    size_t target = (size_t)gtk_text_iter_get_offset(it), at = 0, chars = 0;
    while (chars < target && at < gap_length(&e->gap)) {
        size_t physical = at < e->gap.start ? at : at + e->gap.end - e->gap.start;
        unsigned char c = (unsigned char)e->gap.data[physical];
        at += c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4; chars++;
    }
    return at;
}

static void record(Editor *e, size_t at, const char *text, size_t n, gboolean insert) {
    if (e->replay || e->loading) return;
    if (!e->depth) e->group++;
    while (e->history->len > e->position) g_ptr_array_remove_index(e->history, e->history->len-1);
    Edit *edit = g_new0(Edit, 1);
    *edit = (Edit){at, g_strndup(text, n), n, insert, e->group};
    g_ptr_array_add(e->history, edit); e->position++;
}

static void inserted(GtkTextBuffer *b, GtkTextIter *it, char *text, int n, Editor *e) {
    if (e->loading) return;
    size_t at = byte_offset(e, it);
    if (!gap_insert(&e->gap, at, text, (size_t)n)) {
        g_signal_stop_emission_by_name(b, "insert-text"); fail(e, "Not enough memory to insert text."); return;
    }
    record(e, at, text, (size_t)n, TRUE);
}

static void deleted(GtkTextBuffer *b, GtkTextIter *a, GtkTextIter *z, Editor *e) {
    if (e->loading) return;
    size_t at = byte_offset(e, a), n = byte_offset(e, z)-at;
    char *text = gtk_text_buffer_get_text(b, a, z, TRUE);
    if (!gap_delete(&e->gap, at, n)) g_error("Gap buffer range mismatch");
    record(e, at, text, n, FALSE); g_free(text);
}

static GFile *workspace_parent(Editor *e);

static void refresh_images(Editor *e) {
    GFile *folder = e->file ? g_file_get_parent(e->file) : workspace_parent(e);
    images_refresh(e->images,folder,e->source); g_clear_object(&folder);
}

static void select_current(Editor *e);

static gboolean auto_rename(Editor *e, GError **error);

static gboolean style(gpointer p) {
    Editor *e = p; e->style_source = 0; markdown_style(e->buffer);
    GError *error = NULL; auto_rename(e, &error); g_clear_error(&error);
    refresh_images(e);
    return G_SOURCE_REMOVE;
}

static void format_inserted(GtkTextBuffer *buffer, GtkTextIter *at, char *text, int length, Editor *e) {
    (void)at; (void)text; (void)length;
    if (e->loading) return;
    markdown_style(buffer); refresh_images(e);
}

static void format_deleted(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end, Editor *e) {
    (void)start; (void)end;
    if (e->loading) return;
    markdown_style(buffer); refresh_images(e);
}

static void changed(GtkTextBuffer *b, Editor *e) {
    if (e->view) gtk_widget_queue_draw(e->view);
    (void)b; if (e->loading) return;
    title(e); if (e->style_source) g_source_remove(e->style_source);
    e->style_source = g_timeout_add(80, style, e);
}

static void begin(GtkTextBuffer *b, Editor *e) { (void)b; if (!e->depth++) e->group++; }

static void end(GtkTextBuffer *b, Editor *e) { (void)b; if (e->depth) e->depth--; }

static void history(Editor *e, gboolean redo) {
    if ((redo && e->position == e->history->len) || (!redo && !e->position)) return;
    guint group = ((Edit *)g_ptr_array_index(e->history, redo ? e->position : e->position-1))->group;
    e->replay = TRUE;
    do {
        Edit *edit = g_ptr_array_index(e->history, redo ? e->position++ : --e->position);
        char *s = gap_text(&e->gap); if (!s) g_error("Out of memory");
        int offset = (int)g_utf8_strlen(s, (gssize)edit->at); free(s);
        GtkTextIter a, z; gtk_text_buffer_get_iter_at_offset(e->buffer, &a, offset);
        if (edit->insert == redo) gtk_text_buffer_insert(e->buffer, &a, edit->text, (int)edit->n);
        else { z = a; gtk_text_iter_forward_chars(&z, (int)g_utf8_strlen(edit->text, (gssize)edit->n)); gtk_text_buffer_delete(e->buffer, &a, &z); }
        gtk_text_buffer_place_cursor(e->buffer, &a);
    } while ((redo ? e->position < e->history->len : e->position > 0) &&
        ((Edit *)g_ptr_array_index(e->history, redo ? e->position : e->position-1))->group == group);
    e->replay = FALSE;
}

static GFile *workspace_parent(Editor *e);

static void workspace_refresh(Editor *e);

static GFile *choose(Editor *e, gboolean save) {
    GtkFileChooserNative *d = gtk_file_chooser_native_new(save ? "Save Markdown" : "Open Markdown", GTK_WINDOW(e->window),
        save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN, save ? "Save" : "Open", "Cancel");
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
    if (save) {
        char *name = e->file ? g_file_get_basename(e->file) : g_strdup("Untitled.md");
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), name); g_free(name);
        GFile *folder = e->file ? g_file_get_parent(e->file) : workspace_parent(e);
        if (folder) { gtk_file_chooser_set_current_folder_file(GTK_FILE_CHOOSER(d), folder, NULL); g_object_unref(folder); }
    }
    GtkFileFilter *filter = gtk_file_filter_new(); gtk_file_filter_set_name(filter, "Markdown and text");
    gtk_file_filter_add_pattern(filter, "*.md"); gtk_file_filter_add_pattern(filter, "*.markdown"); gtk_file_filter_add_pattern(filter, "*.txt");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), filter);
    filter = gtk_file_filter_new(); gtk_file_filter_set_name(filter, "All files"); gtk_file_filter_add_pattern(filter, "*"); gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), filter);
    GFile *file = gtk_native_dialog_run(GTK_NATIVE_DIALOG(d)) == GTK_RESPONSE_ACCEPT ? gtk_file_chooser_get_file(GTK_FILE_CHOOSER(d)) : NULL;
    g_object_unref(d); return file;
}

static gboolean save(Editor *e, gboolean as) {
    GError *rename_error = NULL;
    if (!as && !auto_rename(e, &rename_error)) { fail(e, rename_error->message); g_clear_error(&rename_error); return FALSE; }
    GFile *file = as || !e->file ? choose(e, TRUE) : g_object_ref(e->file);
    if (!file) return FALSE;
    char *s = gap_text(&e->gap), *etag = NULL; GError *error = NULL;
    gboolean same = e->file && g_file_equal(file, e->file);
    gboolean ok = s && g_file_replace_contents(file, s, gap_length(&e->gap), same ? e->etag : NULL,
        FALSE, G_FILE_CREATE_NONE, &etag, NULL, &error);
    if (ok) {
        if (e->drafts && e->file) { char *uri = g_file_get_uri(e->file); g_hash_table_remove(e->drafts, uri); g_free(uri); }
        e->auto_name = FALSE;
        g_set_object(&e->file, file); g_free(e->etag); e->etag = etag;
        g_free(e->saved); e->saved = g_strdup(s); title(e);
        workspace_refresh(e);
    } else { fail(e, error ? error->message : "Not enough memory to save."); g_clear_error(&error); }
    free(s); g_object_unref(file); return ok;
}

static gboolean discard(Editor *e) {
    if (!dirty(e)) return TRUE;
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(e->window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Save your changes?");
    gtk_dialog_add_buttons(GTK_DIALOG(d), "Cancel", GTK_RESPONSE_CANCEL, "Discard", GTK_RESPONSE_NO, "Save", GTK_RESPONSE_YES, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_YES);
    int response = gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d);
    return response == GTK_RESPONSE_NO || (response == GTK_RESPONSE_YES && save(e, FALSE));
}

static gboolean load(Editor *e, GFile *file) {
    char *text = NULL, *etag = NULL; gsize n = 0; GError *error = NULL; Gap next = {0};
    if (file && !g_file_load_contents(file, NULL, &text, &n, &etag, &error)) {
        fail(e, error->message); g_clear_error(&error); return FALSE;
    }
    if (n > INT_MAX || (text && (!g_utf8_validate(text, (gssize)n, NULL) || memchr(text, 0, n)))) {
        fail(e, "Open a UTF-8 text file without NUL bytes (under 2 GiB)."); g_free(text); g_free(etag); return FALSE;
    }
    if (!gap_insert(&next, 0, text ? text : "", n)) { fail(e, "Not enough memory to open this file."); g_free(text); g_free(etag); return FALSE; }
    e->document++;
    e->loading = TRUE; gtk_text_buffer_set_text(e->buffer, text ? text : "", (int)n); e->loading = FALSE;
    gap_free(&e->gap); e->gap = next; g_set_object(&e->file, file);
    char *uri = file ? g_file_get_uri(file) : NULL;
    e->auto_name = uri && e->drafts && g_hash_table_contains(e->drafts, uri); g_free(uri);
    g_free(e->etag); e->etag = etag; g_free(e->saved); e->saved = text ? text : g_strdup("");
    g_ptr_array_set_size(e->history, 0); e->position = 0; title(e);
    if (e->style_source) g_source_remove(e->style_source);
    style(e); return TRUE;
}

static GFile *workspace_parent(Editor *e) {
    if (!e->workspace) return NULL;
    GtkTreeIter it; GtkTreeModel *model;
    if (e->tree && gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(e->tree)), &model, &it)) {
        GFile *file = NULL; gboolean directory;
        gtk_tree_model_get(model, &it, WS_FILE, &file, WS_DIRECTORY, &directory, -1);
        if (file) {
            if (directory) return file;
            GFile *parent = g_file_get_parent(file); g_object_unref(file); return parent;
        }
    }
    return g_object_ref(e->workspace);
}

static char *workspace_state_path(void) {
    return g_build_filename(g_get_user_config_dir(), "writr", "workspace", NULL);
}

static void remember_workspace(Editor *e) {
    char *path = workspace_state_path(); GError *error = NULL;
    if (!workspace_write_state(path,e->workspace,&error)) {
        fail(e,error->message); g_clear_error(&error);
    }
    g_free(path);
}

static void mark_draft(Editor *e, GFile *file) {
    char *name = g_file_get_basename(file);
    gboolean numbered = FALSE;
    if (g_str_has_prefix(name, "Untitled ")) {
        const char *p = name + strlen("Untitled ");
        const char *digits = p; while (g_ascii_isdigit(*p)) p++;
        numbered = p > digits && !strcmp(p, ".md");
    }
    if (e->drafts && (!g_ascii_strcasecmp(name, "Untitled.md") || !g_ascii_strcasecmp(name, "undefined.md") || numbered))
        g_hash_table_add(e->drafts, g_file_get_uri(file));
    g_free(name);
}

static gboolean set_workspace(Editor *e, GFile *folder) {
    GError *error = NULL; GFile *created = NULL;
    if (!workspace_seed(folder, &created, &error) || !workspace_root(e->store, folder, &error)) {
        fail(e, error->message); g_clear_error(&error); g_clear_object(&created); return FALSE;
    }
    if (created) { mark_draft(e, created); g_object_unref(created); }
    gboolean changed = !e->workspace || !g_file_equal(e->workspace,folder);
    g_set_object(&e->workspace, folder);
    if (changed) remember_workspace(e);
    char *name = g_file_get_parse_name(folder);
    if (e->tree) gtk_widget_set_tooltip_text(e->tree, name);
    g_free(name); return TRUE;
}

static void prompt(Editor *e, InputMode mode);

static void choose_workspace(Editor *e) {
    prompt(e, INPUT_WORKSPACE);
}

static void remember_expanded(GtkTreeView *tree, GtkTreePath *path, gpointer data) {
    GtkTreeIter it; GFile *file = NULL;
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    if (gtk_tree_model_get_iter(model, &it, path)) gtk_tree_model_get(model, &it, WS_FILE, &file, -1);
    if (file) g_ptr_array_add(data, file);
}

typedef struct { GFile *file; GtkTreePath *path; } Restore;

static gboolean restore_row(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *it, gpointer data) {
    Restore *restore = data;
    GFile *file = NULL; gtk_tree_model_get(model, it, WS_FILE, &file, -1);
    gboolean match = file && g_file_equal(file, restore->file); g_clear_object(&file);
    if (match) restore->path = gtk_tree_path_copy(path);
    return match;
}

static void workspace_refresh(Editor *e) {
    if (!e->workspace || !e->store) return;
    GPtrArray *expanded = g_ptr_array_new_with_free_func(g_object_unref);
    GFile *selected = NULL; GtkTreeIter it; GtkTreeModel *model;
    if (e->tree) {
        gtk_tree_view_map_expanded_rows(GTK_TREE_VIEW(e->tree), remember_expanded, expanded);
        if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(e->tree)), &model, &it))
            gtk_tree_model_get(model, &it, WS_FILE, &selected, -1);
    }
    if (set_workspace(e, e->workspace) && e->tree) {
        for (guint i = 0; i < expanded->len; i++) {
            Restore restore = {g_ptr_array_index(expanded, i), NULL};
            gtk_tree_model_foreach(GTK_TREE_MODEL(e->store), restore_row, &restore);
            if (restore.path) {
                gtk_tree_view_expand_row(GTK_TREE_VIEW(e->tree), restore.path, FALSE);
                gtk_tree_path_free(restore.path);
            }
        }
        if (selected) {
            Restore restore = {selected, NULL};
            gtk_tree_model_foreach(GTK_TREE_MODEL(e->store), restore_row, &restore);
            if (restore.path) {
                gtk_tree_view_set_cursor(GTK_TREE_VIEW(e->tree), restore.path, NULL, FALSE);
                gtk_tree_path_free(restore.path);
            }
        }
    }
    g_clear_object(&selected); g_ptr_array_unref(expanded);
}

static void toggle_tree(Editor *e) {
    if (!e->workspace) { choose_workspace(e); return; }
    if (gtk_widget_get_visible(e->sidebar)) {
        gtk_widget_hide(e->sidebar); gtk_widget_grab_focus(e->view);
    } else {
        workspace_refresh(e); gtk_widget_show(e->sidebar); gtk_widget_grab_focus(e->tree);
    }
}

static gboolean expand_folder(GtkTreeView *tree, GtkTreeIter *it, GtkTreePath *path, Editor *e) {
    (void)tree; (void)path; gboolean loaded, directory; GFile *file = NULL;
    gtk_tree_model_get(GTK_TREE_MODEL(e->store), it, WS_FILE, &file, WS_DIRECTORY, &directory, WS_LOADED, &loaded, -1);
    GError *error = NULL;
    gboolean ok = !directory || loaded || (file && workspace_populate(e->store, it, file, &error));
    if (error) { fail(e, error->message); g_clear_error(&error); }
    g_clear_object(&file); return !ok;
}

static void activate_note(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *column, Editor *e) {
    (void)column; GtkTreeIter it;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(e->store), &it, path)) return;
    GFile *file = NULL; gboolean directory;
    gtk_tree_model_get(GTK_TREE_MODEL(e->store), &it, WS_FILE, &file, WS_DIRECTORY, &directory, -1);
    if (!file) return;
    if (directory) {
        if (gtk_tree_view_row_expanded(tree, path)) gtk_tree_view_collapse_row(tree, path);
        else gtk_tree_view_expand_row(tree, path, FALSE);
    } else if ((!e->file || !g_file_equal(file, e->file)) && discard(e) && load(e, file)) gtk_widget_grab_focus(e->view);
    g_object_unref(file);
}

static char *heading_filename(const char *text) {
    char **lines = g_strsplit(text, "\n", -1); char *result = NULL; char fence = 0; int fence_length = 0;
    for (int i = 0; lines[i]; i++) {
        char *s = lines[i]; int indent = 0;
        while (indent < 3 && s[indent] == ' ') indent++;
        char c = s[indent]; int run = 0;
        if (c == '`' || c == '~') while (s[indent+run] == c) run++;
        gboolean closing = fence && c == fence && run >= fence_length;
        if (closing) for (int j = indent+run; s[j]; j++) if (!g_ascii_isspace(s[j])) closing = FALSE;
        if (closing) { fence = 0; continue; }
        if (fence) continue;
        if (run >= 3) { fence = c; fence_length = run; continue; }
        if (s[indent] != '#' || s[indent+1] != ' ') continue;
        char *heading = g_strdup(s+indent+2); g_strstrip(heading);
        int n = (int)strlen(heading), tail = n;
        while (tail > 0 && heading[tail-1] == '#') tail--;
        if (tail < n && tail > 0 && g_ascii_isspace(heading[tail-1])) { heading[tail] = 0; g_strchomp(heading); }
        GString *safe = g_string_new(NULL);
        for (char *p = heading; *p;) {
            gunichar ch = g_utf8_get_char(p); char *next = g_utf8_next_char(p);
            if (g_unichar_iscntrl(ch) || ch == '/' || ch == '\\' || ch == ':' || ch == '?' || ch == '*' || ch == '"' || ch == '<' || ch == '>' || ch == '|') g_string_append_c(safe, '-');
            else if (ch != '`' && ch != '_') g_string_append_len(safe, p, next-p);
            if (safe->len >= 180) break;
            p = next;
        }
        g_strstrip(safe->str); safe->len = strlen(safe->str);
        while (safe->len && (safe->str[0] == '.' || safe->str[0] == ' ')) g_string_erase(safe, 0, 1);
        while (safe->len && (safe->str[safe->len-1] == '.' || safe->str[safe->len-1] == ' ')) g_string_truncate(safe, safe->len-1);
        if (safe->len) result = g_strconcat(safe->str, ".md", NULL);
        g_string_free(safe, TRUE); g_free(heading);
        if (result) break;
    }
    g_strfreev(lines); return result;
}

static gboolean auto_rename(Editor *e, GError **error) {
    if (!e->auto_name || !e->file) return TRUE;
    char *text = gap_text(&e->gap);
    if (!text) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Not enough memory to name the note."); return FALSE; }
    char *name = heading_filename(text); free(text);
    if (!name) return TRUE;
    GFile *parent = g_file_get_parent(e->file), *target = g_file_get_child(parent, name);
    g_free(name); g_object_unref(parent);
    if (g_file_equal(target, e->file)) { g_object_unref(target); return TRUE; }
    GFileInfo *info = g_file_query_info(e->file, "etag::value", G_FILE_QUERY_INFO_NONE, NULL, error);
    if (!info) { g_object_unref(target); return FALSE; }
    gboolean same = !g_strcmp0(e->etag, g_file_info_get_etag(info)); g_object_unref(info);
    if (!same) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_WRONG_ETAG, "The note changed on disk. Save As to preserve your edits.");
        g_object_unref(target); return FALSE;
    }
    if (!g_file_move(e->file, target, G_FILE_COPY_NONE, NULL, NULL, NULL, error)) { g_object_unref(target); return FALSE; }
    if (e->drafts) { char *uri = g_file_get_uri(e->file); g_hash_table_remove(e->drafts, uri); g_free(uri); g_hash_table_add(e->drafts, g_file_get_uri(target)); }
    g_set_object(&e->file, target); g_object_unref(target);
    title(e); workspace_refresh(e); select_current(e); return TRUE;
}

static void select_current(Editor *e) {
    if (!e->tree || !e->file || !e->workspace) return;
    GFile *parent = g_file_get_parent(e->file); GPtrArray *ancestors = g_ptr_array_new_with_free_func(g_object_unref);
    while (parent && !g_file_equal(parent,e->workspace) && g_file_has_prefix(parent,e->workspace)) {
        g_ptr_array_add(ancestors,parent); parent = g_file_get_parent(parent);
    }
    g_clear_object(&parent);
    for (guint i = ancestors->len; i > 0; i--) {
        Restore restore = {g_ptr_array_index(ancestors,i-1), NULL};
        gtk_tree_model_foreach(GTK_TREE_MODEL(e->store),restore_row,&restore);
        if (restore.path) { gtk_tree_view_expand_row(GTK_TREE_VIEW(e->tree),restore.path,FALSE); gtk_tree_path_free(restore.path); }
    }
    g_ptr_array_unref(ancestors);
    Restore restore = {e->file,NULL}; gtk_tree_model_foreach(GTK_TREE_MODEL(e->store),restore_row,&restore);
    if (restore.path) { gtk_tree_view_set_cursor(GTK_TREE_VIEW(e->tree),restore.path,NULL,FALSE); gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(e->tree),restore.path,NULL,FALSE,0,0); gtk_tree_path_free(restore.path); }
}

static void navigate(Editor *e, gboolean next) {
    if (!e->workspace) { prompt(e,INPUT_WORKSPACE); return; }
    /* Resolve after Save/Discard: the current note may be renamed by its first save. */
    if (!discard(e)) return;
    GError *error = NULL; GPtrArray *notes = workspace_notes(e->workspace,&error);
    if (!notes) { fail(e,error->message); g_clear_error(&error); return; }
    GFile *file = workspace_neighbor(notes,e->file,next); g_ptr_array_unref(notes);
    if (file && (!e->file || !g_file_equal(file,e->file)) && load(e,file)) {
        select_current(e); if (e->view) gtk_widget_grab_focus(e->view);
    }
    g_clear_object(&file);
}

static void hide_input(Editor *e) {
    gtk_widget_hide(e->input); e->input_mode = INPUT_NONE; g_clear_object(&e->input_parent);
    gtk_widget_set_sensitive(e->view,TRUE); gtk_widget_set_sensitive(e->tree,TRUE); gtk_widget_grab_focus(e->view);
}

static gboolean input_key(GtkWidget *widget, GdkEventKey *event, Editor *e) {
    (void)widget;
    if (event->keyval != GDK_KEY_Escape) return FALSE;
    if (!e->workspace) gtk_window_close(GTK_WINDOW(e->window)); else hide_input(e);
    return TRUE;
}

static void input_enter(GtkEntry *entry, Editor *e) {
    char *name = g_strdup(gtk_entry_get_text(entry)); g_strstrip(name);
    GError *error = NULL; GFile *created = NULL;
    if (e->input_mode == INPUT_WORKSPACE) {
        if (!*name) { g_free(name); return; }
        char *path = !strcmp(name,"~") ? g_strdup(g_get_home_dir()) : g_str_has_prefix(name,"~/") ? g_build_filename(g_get_home_dir(),name+2,NULL) : g_canonicalize_filename(name,NULL);
        GFile *folder = g_file_new_for_path(path); g_free(path);
        gboolean ok = g_file_make_directory_with_parents(folder,NULL,&error);
        if (!ok && g_error_matches(error,G_IO_ERROR,G_IO_ERROR_EXISTS)) { g_clear_error(&error); ok = TRUE; }
        if (ok && set_workspace(e,folder)) {
            hide_input(e); gtk_widget_show(e->sidebar);
            if (!e->file && !dirty(e)) {
                GPtrArray *notes = workspace_notes(folder,&error);
                if (notes && notes->len) load(e,g_ptr_array_index(notes,0));
                if (notes) g_ptr_array_unref(notes);
            }
            select_current(e);
        }
        g_object_unref(folder);
    } else if (e->input_mode == INPUT_NOTE || e->input_mode == INPUT_FOLDER) {
        gboolean folder = e->input_mode == INPUT_FOLDER;
        gboolean default_name = !*name && !folder;
        if (default_name) { g_free(name); name = g_strdup("Untitled.md"); }
        gboolean ok = workspace_create(e->input_parent,name,folder,&created,&error);
        for (guint suffix = 2; !ok && default_name && g_error_matches(error,G_IO_ERROR,G_IO_ERROR_EXISTS); suffix++) {
            g_clear_error(&error); g_free(name); name = g_strdup_printf("Untitled %u.md",suffix);
            ok = workspace_create(e->input_parent,name,FALSE,&created,&error);
        }
        if (ok) {
            if (!folder) mark_draft(e,created);
            hide_input(e); workspace_refresh(e);
            if (!folder && load(e,created)) select_current(e);
            g_object_unref(created);
        }
    }
    if (error) { fail(e,error->message); g_clear_error(&error); }
    g_free(name);
}

static void prompt(Editor *e, InputMode mode) {
    if ((mode == INPUT_NOTE || mode == INPUT_FOLDER) && !e->workspace) mode = INPUT_WORKSPACE;
    if (mode == INPUT_NOTE && !discard(e)) return;
    g_clear_object(&e->input_parent);
    e->input_parent = mode == INPUT_NOTE || mode == INPUT_FOLDER ? workspace_parent(e) : NULL;
    e->input_mode = mode;
    const char *hint = mode == INPUT_WORKSPACE ? "Workspace folder · Enter to create or open" : mode == INPUT_FOLDER ? "Folder name" : "Note name · Enter for Untitled.md";
    gtk_entry_set_placeholder_text(GTK_ENTRY(e->input), hint); gtk_widget_set_tooltip_text(e->input,hint);
    atk_object_set_name(gtk_widget_get_accessible(e->input),hint);
    char *value = mode == INPUT_WORKSPACE ? (e->workspace ? g_file_get_path(e->workspace) : g_build_filename(g_get_home_dir(),"Documents","Notes",NULL)) : g_strdup("");
    gtk_entry_set_text(GTK_ENTRY(e->input),value ? value : ""); g_free(value);
    gtk_widget_set_sensitive(e->view,FALSE); gtk_widget_set_sensitive(e->tree,FALSE);
    gtk_widget_show(e->input); gtk_widget_grab_focus(e->input); gtk_editable_select_region(GTK_EDITABLE(e->input),0,-1);
}

static gboolean startup(gpointer data) {
    Editor *e = data;
    if (!e->workspace) {
        char *path = workspace_state_path();
        GFile *folder = workspace_read_state(path,NULL); g_free(path);
        if (folder) { set_workspace(e,folder); g_object_unref(folder); }
    }
    if (!e->workspace) prompt(e,INPUT_WORKSPACE);
    else {
        if (!e->file) {
            GError *error = NULL; GPtrArray *notes = workspace_notes(e->workspace,&error);
            if (notes && notes->len) load(e,g_ptr_array_index(notes,0));
            if (notes) g_ptr_array_unref(notes);
            if (error) { fail(e,error->message); g_clear_error(&error); }
        }
        select_current(e);
    }
    return G_SOURCE_REMOVE;
}

typedef struct {
    Editor *editor; GtkTextBuffer *buffer; GtkTextMark *start, *end;
    char *selected, *label; guint document; gboolean plain;
} PasteRequest;

static PasteRequest *paste_begin(Editor *e, gboolean plain) {
    PasteRequest *request = g_new0(PasteRequest,1); request->editor = e; request->buffer = g_object_ref(e->buffer);
    request->document = e->document; request->plain = plain;
    GtkTextIter a,z; gboolean selected = gtk_text_buffer_get_selection_bounds(e->buffer,&a,&z);
    if (!selected) { gtk_text_buffer_get_iter_at_mark(e->buffer,&a,gtk_text_buffer_get_insert(e->buffer)); z = a; }
    request->start = gtk_text_buffer_create_mark(e->buffer,NULL,&a,FALSE);
    request->end = gtk_text_buffer_create_mark(e->buffer,NULL,&z,selected);
    request->selected = gtk_text_buffer_get_text(e->buffer,&a,&z,TRUE);
    request->label = gtk_text_buffer_get_text(e->buffer,&a,&z,FALSE);
    return request;
}

static void paste_free(PasteRequest *request) {
    gtk_text_buffer_delete_mark(request->buffer,request->start); gtk_text_buffer_delete_mark(request->buffer,request->end);
    g_object_unref(request->buffer); g_free(request->selected); g_free(request->label); g_free(request);
}

static gboolean paste_valid(PasteRequest *request) {
    Editor *e = request->editor;
    if (e->closed || request->document != e->document || e->input_mode != INPUT_NONE) return FALSE;
    GtkTextIter a,z;
    gtk_text_buffer_get_iter_at_mark(request->buffer,&a,request->start); gtk_text_buffer_get_iter_at_mark(request->buffer,&z,request->end);
    if (gtk_text_iter_compare(&a,&z) > 0) return FALSE;
    char *now = gtk_text_buffer_get_text(request->buffer,&a,&z,TRUE);
    gboolean unchanged = !strcmp(now,request->selected); g_free(now); return unchanged;
}

static void paste_insert(PasteRequest *request, const char *text) {
    if (!text || !paste_valid(request)) return;
    GtkTextIter a,z;
    gtk_text_buffer_get_iter_at_mark(request->buffer,&a,request->start); gtk_text_buffer_get_iter_at_mark(request->buffer,&z,request->end);
    gtk_text_buffer_begin_user_action(request->buffer);
    gtk_text_buffer_delete(request->buffer,&a,&z); gtk_text_buffer_insert(request->buffer,&a,text,-1);
    gtk_text_buffer_place_cursor(request->buffer,&a); gtk_text_buffer_end_user_action(request->buffer);
    if (request->editor->view) gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(request->editor->view),gtk_text_buffer_get_insert(request->buffer));
}

static void received_text(GtkClipboard *clipboard, const char *text, gpointer data) {
    (void)clipboard; PasteRequest *request = data;
    if (text && paste_valid(request)) {
        char *markdown = NULL;
        if (!request->plain) {
            GtkTextIter at; gtk_text_buffer_get_iter_at_mark(request->buffer,&at,request->start);
            char *source = gap_text(&request->editor->gap);
            if (source && !paste_code_at(source,byte_offset(request->editor,&at))) markdown = paste_link(text,request->label);
            free(source);
        }
        paste_insert(request,markdown ? markdown : text); g_free(markdown);
    }
    paste_free(request);
}

static void received_image(GtkClipboard *clipboard, GdkPixbuf *image, gpointer data);

static void received_uris(GtkClipboard *clipboard, char **uris, gpointer data) {
    PasteRequest *request = data;
    if (!paste_valid(request)) { paste_free(request); return; }
    if (uris && uris[0] && !uris[1]) {
        GFile *file = g_file_new_for_uri(uris[0]);
        if (g_file_is_native(file)) {
            GdkPixbuf *image = image_read(file,NULL);
            if (image) { g_object_unref(file); received_image(clipboard,image,request); g_object_unref(image); return; }
        }
        g_object_unref(file);
    }
    gtk_clipboard_request_text(clipboard,received_text,request);
}

static void received_image(GtkClipboard *clipboard, GdkPixbuf *image, gpointer data) {
    PasteRequest *request = data;
    if (!paste_valid(request)) { paste_free(request); return; }
    if (!image) { gtk_clipboard_request_uris(clipboard,received_uris,request); return; }
    Editor *e = request->editor;
    GFile *folder = e->file ? g_file_get_parent(e->file) : workspace_parent(e), *asset = NULL;
    GError *error = NULL; char *markdown = paste_image(image,folder,&asset,&error);
    g_clear_object(&folder);
    if (markdown) {
        GtkTextIter a,z; gtk_text_buffer_get_iter_at_mark(request->buffer,&a,request->start); gtk_text_buffer_get_iter_at_mark(request->buffer,&z,request->end);
        char *block = g_strconcat(gtk_text_iter_starts_line(&a) ? "" : "\n",markdown,gtk_text_iter_get_char(&z) == '\n' ? "" : "\n",NULL);
        paste_insert(request,block); g_free(block); g_free(markdown);
        if (e->style_source) g_source_remove(e->style_source);
        style(e);
    }
    else { fail(e,error->message); g_clear_error(&error); }
    g_clear_object(&asset); paste_free(request);
}

static void copy_image(GtkTextView *view, Editor *e) {
    if (images_copy(e->images,FALSE)) g_signal_stop_emission_by_name(view,"copy-clipboard");
}

static void cut_image(GtkTextView *view, Editor *e) {
    if (images_copy(e->images,TRUE)) g_signal_stop_emission_by_name(view,"cut-clipboard");
}

static void paste_clipboard(GtkTextView *view, Editor *e) {
    g_signal_stop_emission_by_name(view,"paste-clipboard");
    if (e->input_mode != INPUT_NONE || e->closed) return;
    PasteRequest *request = paste_begin(e,FALSE);
    gtk_clipboard_request_image(gtk_widget_get_clipboard(GTK_WIDGET(view),GDK_SELECTION_CLIPBOARD),received_image,request);
}

static gboolean key(GtkWidget *w, GdkEventKey *event, Editor *e) {
    (void)w;
    if (e->input_mode != INPUT_NONE) return FALSE;
    if (!(event->state & GDK_CONTROL_MASK)) return FALSE;
    guint k = gdk_keyval_to_lower(event->keyval); gboolean shift = event->state & GDK_SHIFT_MASK;
    if (k == GDK_KEY_v && shift) {
        PasteRequest *request = paste_begin(e,TRUE);
        gtk_clipboard_request_text(gtk_widget_get_clipboard(e->view,GDK_SELECTION_CLIPBOARD),received_text,request);
    }
    else if (k == GDK_KEY_Page_Down || k == GDK_KEY_Page_Up) navigate(e,k == GDK_KEY_Page_Down);
    else if (k == GDK_KEY_t) toggle_tree(e);
    else if (k == GDK_KEY_o && shift) choose_workspace(e);
    else if (k == GDK_KEY_n && (event->state & GDK_MOD1_MASK)) prompt(e, INPUT_FOLDER);
    else if (k == GDK_KEY_n && shift) prompt(e, INPUT_NOTE);
    else if (k == GDK_KEY_r && shift) workspace_refresh(e);
    else if (k == GDK_KEY_m && shift) { e->source = !e->source; markdown_source(e->buffer, e->source); refresh_images(e); }
    else if (k == GDK_KEY_s) save(e, shift);
    else if (k == GDK_KEY_z || k == GDK_KEY_y) history(e, shift || k == GDK_KEY_y);
    else if (k == GDK_KEY_q) gtk_window_close(GTK_WINDOW(e->window));
    else if (k == GDK_KEY_n) prompt(e, INPUT_NOTE);
    else if (k == GDK_KEY_o || k == GDK_KEY_w) {
        if (discard(e)) {
            if (k == GDK_KEY_o) { GFile *f = choose(e, FALSE); if (f) { load(e, f); g_object_unref(f); } }
            else load(e, NULL);
        }
    } else return FALSE;
    return TRUE;
}

static void destroyed(GtkWidget *w, Editor *e) { (void)w; e->closed = TRUE; gtk_main_quit(); }

static gboolean closing(GtkWidget *w, GdkEvent *event, Editor *e) { (void)w; (void)event; return !discard(e); }

static gboolean empty_hints(GtkWidget *widget, cairo_t *cr, Editor *e) {
    if (gtk_text_buffer_get_char_count(e->buffer)) return FALSE;
    GdkWindow *window = gtk_text_view_get_window(GTK_TEXT_VIEW(widget),GTK_TEXT_WINDOW_TEXT);
    if (!window || !gtk_cairo_should_draw_window(cr,window)) return FALSE;
    GdkRGBA color;
    gtk_style_context_get_color(gtk_widget_get_style_context(widget),GTK_STATE_FLAG_NORMAL,&color);
    PangoLayout *layout = gtk_widget_create_pango_layout(widget,NULL);
    pango_layout_set_markup(layout,"<span size='large'>Start typing…</span>",-1);
    GtkTextIter cursor; GdkRectangle cursor_rect;
    gtk_text_buffer_get_iter_at_mark(e->buffer,&cursor,gtk_text_buffer_get_insert(e->buffer));
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(widget),&cursor,&cursor_rect);
    int x,y;
    gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(widget),GTK_TEXT_WINDOW_TEXT,
        cursor_rect.x,cursor_rect.y,&x,&y);
    cairo_save(cr); gtk_cairo_transform_to_window(cr,widget,window);
    cairo_set_source_rgba(cr,color.red,color.green,color.blue,color.alpha*.35);
    cairo_move_to(cr,x,y); pango_cairo_show_layout(cr,layout);
    pango_layout_set_markup(layout,
        "<span size='small'>Ctrl+N   New note\nCtrl+O   Open note\nCtrl+S   Save\nCtrl+T   Workspace tree\nCtrl+PageUp / PageDown   Previous / next note</span>",-1);
    pango_layout_set_alignment(layout,PANGO_ALIGN_CENTER);
    int width,height; pango_layout_get_pixel_size(layout,&width,&height);
    cairo_move_to(cr,MAX(0,(gdk_window_get_width(window)-width)/2),
        MAX(0,(gdk_window_get_height(window)-height)/2));
    pango_cairo_show_layout(cr,layout);
    cairo_restore(cr); g_object_unref(layout); return FALSE;
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    Editor e = {0}; e.drafts = g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL); e.history = g_ptr_array_new_with_free_func(edit_free); e.saved = g_strdup("");
    e.window = gtk_window_new(GTK_WINDOW_TOPLEVEL); gtk_window_set_default_size(GTK_WINDOW(e.window), 880, 760);
    gtk_window_set_decorated(GTK_WINDOW(e.window), FALSE);
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL), *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *overlay = gtk_overlay_new(); gtk_container_add(GTK_CONTAINER(e.window), overlay);
    gtk_container_add(GTK_CONTAINER(overlay), paned);
    e.input = gtk_entry_new(); gtk_widget_set_halign(e.input,GTK_ALIGN_CENTER); gtk_widget_set_valign(e.input,GTK_ALIGN_START);
    gtk_widget_set_margin_top(e.input,32); gtk_widget_set_size_request(e.input,480,-1);
    gtk_widget_set_no_show_all(e.input,TRUE); gtk_overlay_add_overlay(GTK_OVERLAY(overlay),e.input);
    g_signal_connect(e.input,"activate",G_CALLBACK(input_enter),&e);
    g_signal_connect(e.input,"key-press-event",G_CALLBACK(input_key),&e);
    e.store = workspace_store(); e.tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(e.store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(e.tree), FALSE);
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(e.tree), -1, "Notes", renderer, "text", WS_NAME, NULL);
    gtk_widget_set_tooltip_text(e.tree, "Workspace · Ctrl+Shift+O to open a folder");
    e.sidebar = gtk_scrolled_window_new(NULL, NULL); gtk_container_add(GTK_CONTAINER(e.sidebar), e.tree);
    gtk_widget_set_size_request(e.sidebar, 180, -1);
    gtk_widget_show_all(e.sidebar); gtk_widget_hide(e.sidebar);
    gtk_widget_set_no_show_all(e.sidebar, TRUE);
    gtk_paned_pack1(GTK_PANED(paned), e.sidebar, FALSE, FALSE); gtk_paned_pack2(GTK_PANED(paned), scroll, TRUE, FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 230);
    g_signal_connect(e.tree, "test-expand-row", G_CALLBACK(expand_folder), &e);
    g_signal_connect(e.tree, "row-activated", G_CALLBACK(activate_note), &e);
    e.view = gtk_text_view_new(); e.buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(e.view));
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(e.view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(e.view), 48); gtk_text_view_set_right_margin(GTK_TEXT_VIEW(e.view), 48);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(e.view), 32); gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(e.view), 40);
    gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(e.view), 5); gtk_container_add(GTK_CONTAINER(scroll), e.view);
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, "textview { font-family: 'Open Sans', sans-serif; font-size: 17px; }", -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(e.view), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION); g_object_unref(css);
    markdown_init(e.buffer); e.images = images_new(GTK_TEXT_VIEW(e.view));
    g_signal_connect_after(e.view,"draw",G_CALLBACK(empty_hints),&e);
    g_signal_connect(e.buffer, "insert-text", G_CALLBACK(inserted), &e); g_signal_connect(e.buffer, "delete-range", G_CALLBACK(deleted), &e);
    g_signal_connect_after(e.buffer,"insert-text",G_CALLBACK(format_inserted),&e);
    g_signal_connect_after(e.buffer,"delete-range",G_CALLBACK(format_deleted),&e);
    g_signal_connect(e.buffer, "changed", G_CALLBACK(changed), &e); g_signal_connect(e.buffer, "begin-user-action", G_CALLBACK(begin), &e); g_signal_connect(e.buffer, "end-user-action", G_CALLBACK(end), &e);
    g_signal_connect(e.window, "key-press-event", G_CALLBACK(key), &e); g_signal_connect(e.window, "delete-event", G_CALLBACK(closing), &e);
    g_signal_connect(e.window, "destroy", G_CALLBACK(destroyed), &e);
    g_signal_connect(e.view,"paste-clipboard",G_CALLBACK(paste_clipboard),&e);
    g_signal_connect(e.view,"copy-clipboard",G_CALLBACK(copy_image),&e);
    g_signal_connect(e.view,"cut-clipboard",G_CALLBACK(cut_image),&e);
    gtk_widget_show_all(e.window); gtk_widget_show_all(e.tree); gtk_widget_grab_focus(e.view);
    if (argc > 1) {
        GFile *file = g_file_new_for_commandline_arg(argv[1]);
        if (g_file_query_file_type(file, G_FILE_QUERY_INFO_NONE, NULL) == G_FILE_TYPE_DIRECTORY) {
            if (set_workspace(&e, file)) gtk_widget_show(e.sidebar);
            title(&e);
        } else {
            GFile *parent = g_file_get_parent(file); if (parent) { set_workspace(&e,parent); g_object_unref(parent); }
            load(&e, file);
        }
        g_object_unref(file);
    } else title(&e);
    guint startup_source = g_idle_add(startup,&e);
    gtk_main();
    if (g_main_context_find_source_by_id(NULL,startup_source)) g_source_remove(startup_source);
    if (e.style_source) g_source_remove(e.style_source);
    images_free(e.images);
    g_clear_object(&e.store); g_clear_object(&e.workspace); g_clear_object(&e.input_parent); g_hash_table_unref(e.drafts);
    gap_free(&e.gap); g_ptr_array_unref(e.history); g_clear_object(&e.file); g_free(e.etag); g_free(e.saved); return 0;
}
