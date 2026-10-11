#include "workspace.h"
#include <glib/gstdio.h>
#include <assert.h>
#include <string.h>

static gboolean find(GtkTreeStore *store, GtkTreeIter *parent, const char *name, GtkTreeIter *out) {
    GtkTreeIter it;
    if (!gtk_tree_model_iter_children(GTK_TREE_MODEL(store), &it, parent)) return FALSE;
    do {
        char *value = NULL; gtk_tree_model_get(GTK_TREE_MODEL(store), &it, WS_NAME, &value, -1);
        gboolean match = !g_strcmp0(value,name); g_free(value);
        if (match) { *out = it; return TRUE; }
    } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &it));
    return FALSE;
}

int main(void) {
    char *path = g_dir_make_tmp("writr-workspace-XXXXXX",NULL); assert(path);
    GFile *root = g_file_new_for_path(path), *folder = NULL, *note = NULL, *nested = NULL;
    GError *error = NULL;
    assert(workspace_create(root,"Project",TRUE,&folder,&error));
    GtkTreeStore *empty_store = workspace_store(); GtkTreeIter empty_folder,empty_child;
    assert(workspace_root(empty_store,root,&error)); assert(find(empty_store,NULL,"Project",&empty_folder));
    assert(workspace_populate(empty_store,&empty_folder,folder,&error));
    assert(gtk_tree_model_iter_has_child(GTK_TREE_MODEL(empty_store),&empty_folder));
    assert(find(empty_store,&empty_folder,"(empty)",&empty_child));
    GFile *placeholder = NULL; gboolean empty_loaded = FALSE;
    gtk_tree_model_get(GTK_TREE_MODEL(empty_store),&empty_folder,WS_LOADED,&empty_loaded,-1); assert(empty_loaded);
    gtk_tree_model_get(GTK_TREE_MODEL(empty_store),&empty_child,WS_FILE,&placeholder,-1); assert(!placeholder);
    g_object_unref(empty_store);
    GFile *path_note = NULL;
    char *missing_parent = NULL;
    assert(workspace_missing_parent(root,"Project/Deep/New",&missing_parent,&error));
    assert(!strcmp(missing_parent,"Project/Deep")); g_free(missing_parent);
    GFile *uncreated = g_file_get_child(folder,"Deep"); assert(!g_file_query_exists(uncreated,NULL)); g_object_unref(uncreated);
    assert(workspace_create(root,"Project/Deep/New",FALSE,&path_note,&error));
    assert(workspace_missing_parent(root,"Project/Deep/Another",&missing_parent,&error)); assert(!missing_parent);
    char *path_name = g_file_get_basename(path_note); assert(!strcmp(path_name,"New.md")); g_free(path_name);
    GFile *deep = g_file_get_parent(path_note), *project = g_file_get_parent(deep); assert(g_file_equal(project,folder));
    assert(g_file_delete(path_note,NULL,&error)); assert(g_file_delete(deep,NULL,&error));
    g_object_unref(path_note); g_object_unref(deep); g_object_unref(project);
    GFile *deleted_note = NULL;
    assert(workspace_create(root,"Delete/sub/note",FALSE,&deleted_note,&error));
    GFile *deleted_sub = g_file_get_parent(deleted_note), *deleted_folder = g_file_get_parent(deleted_sub);
    GFile *deleted_hidden = g_file_get_child(deleted_folder,".hidden"), *deleted_link = g_file_get_child(deleted_folder,"link");
    assert(g_file_replace_contents(deleted_hidden,"hidden",6,NULL,FALSE,G_FILE_CREATE_NONE,NULL,NULL,&error));
    assert(g_file_make_symbolic_link(deleted_link,path,NULL,&error));
    assert(workspace_delete(deleted_note,&error)); assert(!g_file_query_exists(deleted_note,NULL));
    assert(workspace_delete(deleted_folder,&error)); assert(!g_file_query_exists(deleted_folder,NULL));
    assert(g_file_query_exists(root,NULL) && g_file_query_exists(folder,NULL));
    g_object_unref(deleted_note); g_object_unref(deleted_sub); g_object_unref(deleted_folder);
    g_object_unref(deleted_hidden); g_object_unref(deleted_link);
    assert(workspace_create(root,"NewFolder/Sub/note.md",FALSE,&path_note,&error));
    deep = g_file_get_parent(path_note); project = g_file_get_parent(deep);
    assert(g_file_delete(path_note,NULL,&error)); assert(g_file_delete(deep,NULL,&error)); assert(g_file_delete(project,NULL,&error));
    g_object_unref(path_note); g_object_unref(deep); g_object_unref(project);
    const char *invalid[] = {"/absolute", "Project/../escape", "Project//note", "Project/.hidden/note", "Project/", NULL};
    for (int i = 0; invalid[i]; i++) {
        assert(!workspace_missing_parent(root,invalid[i],&missing_parent,&error)); assert(!missing_parent); g_clear_error(&error);
        assert(!workspace_create(root,invalid[i],FALSE,&path_note,&error)); assert(!path_note); g_clear_error(&error);
    }
    assert(workspace_create(root,"Résumé",FALSE,&note,&error));
    assert(workspace_create(folder,"Nested",FALSE,&nested,&error));
    GFile *duplicate = NULL; assert(!workspace_create(root,"Résumé",FALSE,&duplicate,&error));
    assert(g_error_matches(error,G_IO_ERROR,G_IO_ERROR_EXISTS)); g_clear_error(&error);
    assert(!workspace_create(root,"../escape",FALSE,&duplicate,&error)); g_clear_error(&error);
    assert(!workspace_create(root,".hidden",TRUE,&duplicate,&error)); g_clear_error(&error);
    char *ignored = g_build_filename(path,"image.png",NULL); assert(g_file_set_contents(ignored,"x",1,NULL));
    char *hidden = g_build_filename(path,".hidden.md",NULL); assert(g_file_set_contents(hidden,"x",1,NULL));
    char *symlink_path = g_build_filename(path,"loop",NULL); GFile *symlink_file = g_file_new_for_path(symlink_path);
    assert(g_file_make_symbolic_link(symlink_file,path,NULL,&error));
    assert(!workspace_missing_parent(root,"loop/escape",&missing_parent,&error)); assert(!missing_parent); g_clear_error(&error);
    assert(!workspace_create(root,"loop/escape",FALSE,&path_note,&error)); assert(!path_note); g_clear_error(&error);
    GtkTreeStore *store = workspace_store(); assert(workspace_root(store,root,&error));
    assert(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store),NULL) == 2);
    GtkTreeIter it, first; assert(find(store,NULL,"Project",&it));
    assert(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&first));
    char *name; gtk_tree_model_get(GTK_TREE_MODEL(store),&first,WS_NAME,&name,-1); assert(!strcmp(name,"Project")); g_free(name);
    gboolean loaded; gtk_tree_model_get(GTK_TREE_MODEL(store),&it,WS_LOADED,&loaded,-1); assert(!loaded);
    assert(workspace_populate(store,&it,folder,&error));
    GtkTreeIter child; assert(find(store,&it,"Nested",&child));
    assert(workspace_populate(store,&it,folder,&error)); assert(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store),&it) == 1);
    GFile *missing = g_file_get_child(root,"missing"); assert(!workspace_root(store,missing,&error)); g_clear_error(&error);
    assert(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store),NULL) == 2); g_object_unref(missing);
    GPtrArray *notes = workspace_notes(root,&error); assert(notes && notes->len == 2);
    assert(g_file_equal(g_ptr_array_index(notes,0),nested)); assert(g_file_equal(g_ptr_array_index(notes,1),note));
    GFile *next = workspace_neighbor(notes,nested,TRUE); assert(g_file_equal(next,note)); g_object_unref(next);
    next = workspace_neighbor(notes,note,TRUE); assert(g_file_equal(next,nested)); g_object_unref(next);
    next = workspace_neighbor(notes,nested,FALSE); assert(g_file_equal(next,note)); g_object_unref(next);
    g_ptr_array_unref(notes);
    GFile *seed = NULL; assert(workspace_seed(root,&seed,&error)); assert(!seed);
    assert(g_file_delete(nested,NULL,&error)); assert(g_file_delete(folder,NULL,&error)); assert(g_file_delete(note,NULL,&error));
    assert(workspace_seed(root,&seed,&error)); assert(seed);
    char *seed_name = g_file_get_basename(seed); assert(!strcmp(seed_name,"undefined.md")); g_free(seed_name);
    GFile *again = NULL; assert(workspace_seed(root,&again,&error)); assert(!again);
    notes = workspace_notes(root,&error); assert(notes->len == 1); g_ptr_array_unref(notes);
    assert(workspace_root(store,root,&error)); assert(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store),NULL) == 1);
    assert(g_file_delete(seed,NULL,&error)); g_object_unref(seed);
    g_remove(ignored); g_remove(hidden); assert(g_file_delete(symlink_file,NULL,&error)); assert(g_file_delete(root,NULL,&error));
    g_object_unref(nested); g_object_unref(folder); g_object_unref(note); g_object_unref(root); g_object_unref(symlink_file); g_object_unref(store);
    g_free(path); g_free(ignored); g_free(hidden); g_free(symlink_path);
    g_print("workspace: lazy folders, sorting, Unicode names, filtering, safe creation and failed switches passed\n");
}
