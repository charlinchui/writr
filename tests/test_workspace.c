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
    GtkTreeStore *store = workspace_store(); assert(workspace_root(store,root,&error));
    assert(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store),NULL) == 2);
    GtkTreeIter it, first; assert(find(store,NULL,"Project",&it));
    assert(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&first));
    char *name; gtk_tree_model_get(GTK_TREE_MODEL(store),&first,WS_NAME,&name,-1); assert(!strcmp(name,"Project")); g_free(name);
    gboolean loaded; gtk_tree_model_get(GTK_TREE_MODEL(store),&it,WS_LOADED,&loaded,-1); assert(!loaded);
    assert(workspace_populate(store,&it,folder,&error));
    GtkTreeIter child; assert(find(store,&it,"Nested.md",&child));
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
