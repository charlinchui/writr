#define main writr_main
#include "main.c"
#undef main
#include <assert.h>
#include <glib/gstdio.h>
static void equal(Editor *e, const char *expected) {
    char *s = gap_text(&e->gap); assert(!strcmp(s, expected)); free(s);
    GtkTextIter a, z; gtk_text_buffer_get_bounds(e->buffer, &a, &z);
    s = gtk_text_buffer_get_text(e->buffer, &a, &z, TRUE); assert(!strcmp(s, expected)); g_free(s);
}

static void tagged(Editor *e, int line, int byte, const char *name) {
    GtkTextIter it; gtk_text_buffer_get_iter_at_line_index(e->buffer, &it, line, byte);
    assert(gtk_text_iter_has_tag(&it, gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(e->buffer), name)));
}

int main(int argc, char **argv) {
    char *config = g_dir_make_tmp("writr-config-XXXXXX",NULL); assert(config);
    g_setenv("XDG_CONFIG_HOME",config,TRUE);
    assert(!skip_folder_confirmation());
    GError *preference_error = NULL; assert(remember_folder_confirmation(&preference_error)); assert(!preference_error);
    assert(skip_folder_confirmation());
    assert(!skip_confirmation("deletion","files-without-asking"));
    assert(!skip_confirmation("deletion","folders-without-asking"));
    assert(remember_confirmation("deletion","files-without-asking",&preference_error));
    assert(skip_confirmation("deletion","files-without-asking"));
    assert(!skip_confirmation("deletion","folders-without-asking")); assert(skip_folder_confirmation());
    assert(remember_confirmation("deletion","folders-without-asking",&preference_error));
    assert(skip_confirmation("deletion","folders-without-asking")); assert(skip_confirmation("deletion","files-without-asking"));
    char *preference_path = settings_path(); assert(g_remove(preference_path) == 0); g_free(preference_path);
    assert(!skip_folder_confirmation());
    gboolean display = gtk_init_check(&argc, &argv);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    g_type_ensure(GDK_TYPE_COLOR);
    G_GNUC_END_IGNORE_DEPRECATIONS
    g_type_ensure(GDK_TYPE_RGBA); g_type_ensure(PANGO_TYPE_FONT_DESCRIPTION);
    Editor e = {0}; e.drafts = g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
    e.buffer = gtk_text_buffer_new(NULL); e.history = g_ptr_array_new_with_free_func(edit_free); e.saved = g_strdup(""); markdown_init(e.buffer);
    g_signal_connect(e.buffer, "insert-text", G_CALLBACK(inserted), &e); g_signal_connect(e.buffer, "delete-range", G_CALLBACK(deleted), &e);
    g_signal_connect_after(e.buffer,"insert-text",G_CALLBACK(format_inserted),&e);
    g_signal_connect_after(e.buffer,"delete-range",G_CALLBACK(format_deleted),&e);
    /* Formatting is already applied on return, without waiting for the event loop. */
    gtk_text_buffer_insert_at_cursor(e.buffer,"#",-1); tagged(&e,0,0,"h1");
    gtk_text_buffer_insert_at_cursor(e.buffer," Title",-1); tagged(&e,0,2,"h1"); tagged(&e,0,0,"syntax");
    GtkTextIter heading_start,heading_end;
    gtk_text_buffer_get_iter_at_offset(e.buffer,&heading_start,1); gtk_text_buffer_get_end_iter(e.buffer,&heading_end);
    gtk_text_buffer_delete(e.buffer,&heading_start,&heading_end); tagged(&e,0,0,"h1");
    gtk_text_buffer_get_start_iter(e.buffer,&heading_start);
    assert(!gtk_text_iter_has_tag(&heading_start,gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(e.buffer),"syntax")));
    load(&e,NULL);
    g_signal_connect(e.buffer, "begin-user-action", G_CALLBACK(begin), &e); g_signal_connect(e.buffer, "end-user-action", G_CALLBACK(end), &e);
    gtk_text_buffer_insert_at_cursor(e.buffer, "é🙂", -1); equal(&e, "é🙂");
    GtkTextIter a,z; gtk_text_buffer_get_iter_at_offset(e.buffer, &a, 1); gtk_text_buffer_insert(e.buffer, &a, "中", -1); equal(&e,"é中🙂");
    history(&e,FALSE); equal(&e,"é🙂"); history(&e,TRUE); equal(&e,"é中🙂");
    gtk_text_buffer_begin_user_action(e.buffer);
    gtk_text_buffer_get_iter_at_offset(e.buffer,&a,1); gtk_text_buffer_get_iter_at_offset(e.buffer,&z,2); gtk_text_buffer_delete(e.buffer,&a,&z); gtk_text_buffer_insert(e.buffer,&a,"hello",-1);
    gtk_text_buffer_end_user_action(e.buffer); equal(&e,"éhello🙂"); history(&e,FALSE); equal(&e,"é中🙂"); history(&e,TRUE); equal(&e,"éhello🙂");
    assert(shortcut_pressed(NULL,GDK_KEY_z,0,GDK_CONTROL_MASK,&e)); equal(&e,"é中🙂");
    assert(shortcut_pressed(NULL,GDK_KEY_z,0,GDK_CONTROL_MASK|GDK_SHIFT_MASK,&e)); equal(&e,"éhello🙂");
    assert(shortcut_pressed(NULL,GDK_KEY_m,0,GDK_CONTROL_MASK|GDK_SHIFT_MASK,&e)); assert(e.source);
    assert(shortcut_pressed(NULL,GDK_KEY_m,0,GDK_CONTROL_MASK|GDK_SHIFT_MASK,&e)); assert(!e.source);
    assert(!shortcut_pressed(NULL,GDK_KEY_t,0,0,&e));
    assert(shortcut_pressed(NULL,GDK_KEY_d,0,GDK_CONTROL_MASK,&e)); equal(&e,"éhello🙂");
    e.input_mode = INPUT_NOTE;
    assert(!shortcut_pressed(NULL,GDK_KEY_z,0,GDK_CONTROL_MASK,&e)); equal(&e,"éhello🙂");
    e.input_mode = INPUT_NONE;
    /* Repeated T presses are suppressed only until T is released, independently of Ctrl. */
    e.tree_key_down = TRUE;
    assert(shortcut_pressed(NULL,GDK_KEY_t,28,GDK_CONTROL_MASK,&e)); assert(e.tree_key_down);
    shortcut_released(NULL,GDK_KEY_Control_L,37,0,&e); assert(e.tree_key_down);
    shortcut_released(NULL,GDK_KEY_T,28,GDK_CONTROL_MASK,&e); assert(!e.tree_key_down);
    load(&e,NULL); gtk_text_buffer_insert_at_cursor(e.buffer,"# Title\n**bold** and *italic* `x`\n```c\nint n = 42; // comment\nconst char *s = \"hello\";\n```\n~~~python\nreturn True\n~~~\n",-1);
    markdown_style(e.buffer); tagged(&e,0,2,"h1"); tagged(&e,1,2,"bold"); tagged(&e,1,14,"italic"); tagged(&e,1,23,"inline");
    tagged(&e,3,0,"keyword"); tagged(&e,3,8,"number"); tagged(&e,3,12,"muted"); tagged(&e,4,16,"string"); tagged(&e,7,0,"keyword");
    tagged(&e,0,0,"syntax"); tagged(&e,1,0,"syntax"); tagged(&e,1,22,"syntax"); tagged(&e,2,0,"syntax");
    gtk_text_buffer_get_bounds(e.buffer,&a,&z);
    char *visible = gtk_text_buffer_get_text(e.buffer,&a,&z,FALSE);
    assert(g_str_has_prefix(visible,"Title\nbold and italic x\n\nint n = 42; // comment")); g_free(visible);
    char *raw = gap_text(&e.gap); markdown_style(e.buffer); equal(&e,raw);
    markdown_source(e.buffer,TRUE); gtk_text_buffer_get_bounds(e.buffer,&a,&z);
    visible = gtk_text_buffer_get_text(e.buffer,&a,&z,FALSE); assert(!strcmp(visible,raw)); g_free(visible);
    markdown_source(e.buffer,FALSE); free(raw);
    /* Empty constructs retain their markers, including fences with a language. */
    GtkTextBuffer *empty = gtk_text_buffer_new(NULL); markdown_init(empty);
    const char *unfinished = "#\n##   \n> \n** **\n[](/path)\n```c\n  \n```\n~~~\n~~~";
    gtk_text_buffer_set_text(empty,unfinished,-1); markdown_style(empty);
    gtk_text_buffer_get_bounds(empty,&a,&z);
    visible = gtk_text_buffer_get_text(empty,&a,&z,FALSE);
    assert(!strcmp(visible,unfinished)); g_free(visible);
    gtk_text_buffer_set_text(empty,"```c\nint x;\n```",-1); markdown_style(empty);
    gtk_text_buffer_get_bounds(empty,&a,&z); visible = gtk_text_buffer_get_text(empty,&a,&z,FALSE);
    assert(!strcmp(visible,"\nint x;\n")); g_free(visible);
    gtk_text_buffer_set_text(empty,"```c\n\n```",-1); markdown_style(empty);
    gtk_text_buffer_get_bounds(empty,&a,&z); visible = gtk_text_buffer_get_text(empty,&a,&z,FALSE);
    assert(!strcmp(visible,"```c\n\n```")); g_free(visible); g_object_unref(empty);
    /* Editing across invisible delimiters must preserve exact undo data. */
    gtk_text_buffer_get_iter_at_line(e.buffer,&a,1); z=a; gtk_text_iter_forward_line(&z);
    raw = gap_text(&e.gap); gtk_text_buffer_delete(e.buffer,&a,&z); history(&e,FALSE); equal(&e,raw); free(raw);
    load(&e,NULL); gtk_text_buffer_insert_at_cursor(e.buffer,"# Café 🙂\n[link](https://example.com)\n\\*literal*\n```c\n# include <stdio.h>\n```",-1);
    markdown_style(e.buffer); tagged(&e,0,0,"syntax"); tagged(&e,1,0,"syntax"); tagged(&e,1,5,"syntax");
    gtk_text_buffer_get_bounds(e.buffer,&a,&z); visible=gtk_text_buffer_get_text(e.buffer,&a,&z,FALSE);
    assert(!strcmp(visible,"Café 🙂\nlink\n\\*literal*\n\n# include <stdio.h>\n")); g_free(visible);
    load(&e,NULL); gtk_text_buffer_insert_at_cursor(e.buffer,"Read this",-1);
    gtk_text_buffer_get_bounds(e.buffer,&a,&z); gtk_text_buffer_select_range(e.buffer,&a,&z);
    PasteRequest *request = paste_begin(&e,FALSE); received_text(NULL,"https://example.com/a(b)",request);
    equal(&e,"[Read this](https://example.com/a%28b%29)"); history(&e,FALSE); equal(&e,"Read this");
    history(&e,TRUE); equal(&e,"[Read this](https://example.com/a%28b%29)");
    request = paste_begin(&e,FALSE); load(&e,NULL); received_text(NULL,"https://example.com",request); equal(&e,"");
    gtk_text_buffer_insert_at_cursor(e.buffer,"```c\n",-1); request = paste_begin(&e,FALSE);
    received_text(NULL,"https://example.com",request); equal(&e,"```c\nhttps://example.com");
    load(&e,NULL); request = paste_begin(&e,TRUE); received_text(NULL,"https://example.com",request); equal(&e,"https://example.com");
    load(&e,NULL); gtk_text_buffer_insert_at_cursor(e.buffer,"selected",-1); gtk_text_buffer_get_bounds(e.buffer,&a,&z); gtk_text_buffer_select_range(e.buffer,&a,&z);
    request = paste_begin(&e,FALSE); gtk_text_buffer_get_bounds(e.buffer,&a,&z); gtk_text_buffer_delete(e.buffer,&a,&z);
    received_text(NULL,"https://example.com",request); equal(&e,""); /* Do not replace an edited selection. */
    load(&e,NULL); gtk_text_buffer_insert_at_cursor(e.buffer,"![image](assets/test.png)",-1); markdown_style(e.buffer);
    gtk_text_buffer_get_bounds(e.buffer,&a,&z); visible = gtk_text_buffer_get_text(e.buffer,&a,&z,FALSE); assert(!strcmp(visible,"image")); g_free(visible);
    char *dir = g_dir_make_tmp("writr-test-XXXXXX",NULL); assert(dir); char *path = g_build_filename(dir,"test.md",NULL); e.file = g_file_new_for_path(path);
    assert(save(&e,FALSE)); assert(!dirty(&e)); char *expected = gap_text(&e.gap);
    GFile *f = g_object_ref(e.file); assert(load(&e,f)); equal(&e,expected); free(expected);
    gtk_text_buffer_insert_at_cursor(e.buffer,"more",-1); assert(dirty(&e)); assert(save(&e,FALSE)); assert(!dirty(&e));
    char *old_etag = g_strdup(e.etag); g_usleep(1100000); assert(g_file_set_contents(path,"external longer change",-1,NULL));
    GError *error = NULL; assert(!g_file_replace_contents(f,"oops",4,old_etag,FALSE,G_FILE_CREATE_NONE,NULL,NULL,&error)); assert(g_error_matches(error,G_IO_ERROR,G_IO_ERROR_WRONG_ETAG)); g_clear_error(&error); g_free(old_etag);
    /* New placeholder follows H1 until the first explicit save. */
    GFile *root = g_file_new_for_path(dir), *draft = NULL;
    assert(workspace_create(root,"Untitled.md",FALSE,&draft,&error)); mark_draft(&e,draft); assert(load(&e,draft)); assert(e.auto_name);
    gtk_text_buffer_insert_at_cursor(e.buffer,"# Café / Notes\nBody",-1); assert(auto_rename(&e,&error));
    char *name = g_file_get_basename(e.file); assert(!strcmp(name,"Café - Notes.md")); g_free(name);
    assert(!g_file_query_exists(draft,NULL)); assert(g_file_query_exists(e.file,NULL));
    GFile *renamed = g_object_ref(e.file);
    assert(save(&e,FALSE)); assert(!e.auto_name);
    gtk_text_buffer_get_bounds(e.buffer,&a,&z); gtk_text_buffer_delete(e.buffer,&a,&z); gtk_text_buffer_insert_at_cursor(e.buffer,"# Different title",-1);
    assert(auto_rename(&e,&error)); assert(g_file_equal(e.file,renamed)); assert(save(&e,FALSE));
    assert(load(&e,renamed)); assert(!e.auto_name);
    /* Previously existing Untitled.md is not a fresh draft. */
    assert(g_file_set_contents(g_file_peek_path(draft),"# Saved title",-1,NULL));
    assert(load(&e,draft)); assert(!e.auto_name); assert(auto_rename(&e,&error)); assert(g_file_equal(e.file,draft));
    GFile *seed = NULL; assert(workspace_create(root,"undefined.md",FALSE,&seed,&error)); mark_draft(&e,seed); assert(load(&e,seed));
    gtk_text_buffer_insert_at_cursor(e.buffer,"# Untitled",-1);
    assert(!auto_rename(&e,&error)); assert(g_error_matches(error,G_IO_ERROR,G_IO_ERROR_EXISTS)); g_clear_error(&error);
    assert(g_file_equal(e.file,seed)); assert(g_file_query_exists(seed,NULL));
    char *contents = NULL; assert(g_file_load_contents(draft,NULL,&contents,NULL,NULL,&error)); assert(!strcmp(contents,"# Saved title")); g_free(contents);
    /* A heading inside a code fence must not become the filename. */
    name = heading_filename("```md\n# Wrong\n```\n# Right ###"); assert(!strcmp(name,"Right.md")); g_free(name);
    name = heading_filename("~~~md\n# Wrong\n~~~ still code\n# Also wrong\n~~~\n# Final"); assert(!strcmp(name,"Final.md")); g_free(name);
    name = heading_filename("## Subtitle\n# .Hidden: / path"); assert(!strcmp(name,"Hidden- - path.md")); g_free(name);
    assert(!heading_filename("No heading\n# "));
    assert(g_file_delete(seed,NULL,&error)); assert(g_file_delete(draft,NULL,&error)); assert(g_file_delete(renamed,NULL,&error));
    g_object_unref(seed); g_object_unref(draft); g_object_unref(renamed); g_object_unref(root);
    /* Image callbacks insert Markdown, preserve the gap, and form one undo action. */
    load(&e,NULL); e.workspace = g_file_new_for_path(dir);
    GdkPixbuf *image = gdk_pixbuf_new(GDK_COLORSPACE_RGB,TRUE,8,2,2); gdk_pixbuf_fill(image,0xabcdef80);
    request = paste_begin(&e,FALSE); received_image(NULL,image,request);
    char *image_source = gap_text(&e.gap); assert(g_str_has_prefix(image_source,"![image](assets/pasted-")); equal(&e,image_source);
    char *relative = g_strndup(image_source+9,(gsize)(strchr(image_source,')')-(image_source+9))); GFile *asset = g_file_resolve_relative_path(e.workspace,relative); g_free(relative);
    assert(g_file_query_exists(asset,NULL)); history(&e,FALSE); equal(&e,""); history(&e,TRUE); equal(&e,image_source); free(image_source);
    request = paste_begin(&e,FALSE); load(&e,NULL); received_image(NULL,image,request); equal(&e,"");
    assert(g_file_delete(asset,NULL,&error)); g_object_unref(asset); GFile *assets = g_file_get_child(e.workspace,"assets"); assert(g_file_delete(assets,NULL,&error)); g_object_unref(assets);
    g_clear_object(&e.workspace); g_object_unref(image);
    /* Simulate closing/reopening: use isolated config, never the user's state. */
    e.store = workspace_store(); assert(set_workspace(&e,root = g_file_new_for_path(dir)));
    char *state = workspace_state_path(); GFile *remembered = workspace_read_state(state,&error);
    assert(remembered && g_file_equal(remembered,root)); g_object_unref(remembered);
    g_clear_object(&e.workspace); g_clear_object(&e.file);
    startup(&e); assert(e.workspace && g_file_equal(e.workspace,root)); assert(e.file && g_file_equal(e.file,f));
    assert(e.input_mode == INPUT_NONE); /* No workspace prompt on restart. */
    /* Deleting a folder containing the open document opens a surviving note. */
    GFile *deletion_note = NULL;
    assert(workspace_create(root,"Deletion/child",FALSE,&deletion_note,&error)); assert(load(&e,deletion_note));
    GFile *deletion_folder = g_file_get_parent(deletion_note);
    assert(remember_confirmation("deletion","folders-without-asking",&error));
    assert(!skip_confirmation("deletion","files-without-asking"));
    assert(delete_item(&e,deletion_folder)); assert(!g_file_query_exists(deletion_folder,NULL)); assert(e.file && g_file_equal(e.file,f));
    g_object_unref(deletion_note); deletion_note = NULL;
    assert(workspace_create(root,"DeletionNote",FALSE,&deletion_note,&error)); assert(load(&e,deletion_note));
    assert(remember_confirmation("deletion","files-without-asking",&error));
    assert(delete_item(&e,deletion_note)); assert(!g_file_query_exists(deletion_note,NULL)); assert(e.file && g_file_equal(e.file,f));
    assert(!delete_item(&e,root)); assert(g_file_query_exists(root,NULL));
    g_object_unref(deletion_folder); g_object_unref(deletion_note);
    preference_path = settings_path(); assert(g_remove(preference_path) == 0); g_free(preference_path);
    if (display) {
        e.tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(e.store)); g_object_ref_sink(e.tree);
        e.sidebar = gtk_revealer_new(); g_object_ref_sink(e.sidebar);
        e.view = gtk_text_view_new(); g_object_ref_sink(e.view);
        e.window = gtk_window_new(GTK_WINDOW_TOPLEVEL); g_object_ref_sink(e.window);
        install_shortcuts(&e);
        assert(!gtk_tree_view_get_enable_search(GTK_TREE_VIEW(e.tree)));
        assert(gtk_event_controller_get_propagation_phase(e.shortcuts) == GTK_PHASE_CAPTURE);
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
        gtk_container_add(GTK_CONTAINER(e.window),box);
        gtk_container_add(GTK_CONTAINER(e.sidebar),e.tree);
        gtk_box_pack_start(GTK_BOX(box),e.sidebar,TRUE,TRUE,0);
        gtk_box_pack_start(GTK_BOX(box),e.view,TRUE,TRUE,0);
        gtk_widget_show_all(e.window);
        /* test.md must not capture the tree toggle through filename search. */
        for (int i = 0; i < 4; i++) {
            GdkEvent *event = gdk_event_new(GDK_KEY_PRESS);
            event->key.window = g_object_ref(gtk_widget_get_window(e.window));
            event->key.state = GDK_CONTROL_MASK; event->key.keyval = i%2 ? GDK_KEY_T : GDK_KEY_t;
            gdk_event_set_device(event,gdk_seat_get_keyboard(gdk_display_get_default_seat(gdk_display_get_default())));
            gtk_widget_grab_focus(i%2 ? e.tree : e.view);
            gtk_main_do_event(event);
            assert(gtk_revealer_get_reveal_child(GTK_REVEALER(e.sidebar)) == (i%2 == 0));
            /* Auto-repeat or duplicate press delivery must not toggle twice. */
            gtk_main_do_event(event);
            assert(gtk_revealer_get_reveal_child(GTK_REVEALER(e.sidebar)) == (i%2 == 0));
            event->key.type = GDK_KEY_RELEASE;
            gtk_main_do_event(event); assert(!e.tree_key_down); gdk_event_free(event);
        }
        /* Other commands must use the same capture path while the tree has focus. */
        gtk_revealer_set_reveal_child(GTK_REVEALER(e.sidebar),TRUE);
        gtk_widget_grab_focus(e.tree);
        GdkEvent *event = gdk_event_new(GDK_KEY_PRESS);
        event->key.window = g_object_ref(gtk_widget_get_window(e.window));
        event->key.state = GDK_CONTROL_MASK|GDK_SHIFT_MASK; event->key.keyval = GDK_KEY_m;
        gdk_event_set_device(event,gdk_seat_get_keyboard(gdk_display_get_default_seat(gdk_display_get_default())));
        gboolean source_mode = e.source;
        gtk_main_do_event(event); assert(e.source != source_mode);
        gtk_main_do_event(event); assert(e.source == source_mode); gdk_event_free(event);
        event = gdk_event_new(GDK_KEY_PRESS);
        event->key.window = g_object_ref(gtk_widget_get_window(e.window));
        event->key.state = GDK_CONTROL_MASK|GDK_SHIFT_MASK; event->key.keyval = GDK_KEY_r;
        gdk_event_set_device(event,gdk_seat_get_keyboard(gdk_display_get_default_seat(gdk_display_get_default())));
        gtk_main_do_event(event); assert(!gtk_tree_view_get_enable_search(GTK_TREE_VIEW(e.tree))); gdk_event_free(event);
        g_clear_object(&e.shortcuts);
        gtk_widget_destroy(e.window); g_object_unref(e.window); e.window = NULL;
        g_object_unref(e.tree); g_object_unref(e.sidebar); g_object_unref(e.view);
        e.tree = e.sidebar = e.view = NULL;
    } else puts("editor: tree shortcut checks skipped (no display available)");
    /* Legacy native paths restore too, including whitespace around the state. */
    char *legacy = g_strdup_printf("  %s\n",dir); assert(g_file_set_contents(state,legacy,-1,&error)); g_free(legacy);
    remembered = workspace_read_state(state,&error); assert(remembered && g_file_equal(remembered,root)); g_object_unref(remembered);
    assert(workspace_write_state(state,root,&error));
    g_object_unref(root); g_clear_object(&e.store); g_clear_object(&e.workspace);
    assert(g_remove(state) == 0); char *config_dir = g_path_get_dirname(state); assert(g_rmdir(config_dir) == 0);
    assert(g_rmdir(config) == 0); g_free(config_dir); g_free(config); g_free(state);
    g_file_delete(f,NULL,NULL); g_object_unref(f); g_rmdir(dir); g_free(dir); g_free(path);
    gap_free(&e.gap); g_ptr_array_unref(e.history); g_clear_object(&e.file); g_free(e.saved); g_free(e.etag); g_object_unref(e.buffer); g_hash_table_unref(e.drafts);
    puts("editor: Unicode, grouped undo/redo, styling, saves and conflict checks passed"); return 0;
}
