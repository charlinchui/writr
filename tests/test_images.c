#include "images.h"
#include "paste.h"
#include "markdown.h"
#include <glib/gstdio.h>
#include <assert.h>
#include <string.h>
static void boxed_types(void) {
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    g_type_ensure(GDK_TYPE_COLOR);
    G_GNUC_END_IGNORE_DEPRECATIONS
    g_type_ensure(GDK_TYPE_RGBA); g_type_ensure(PANGO_TYPE_FONT_DESCRIPTION);
}
static void unchanged(GtkTextBuffer *buffer, const char *expected) {
    GtkTextIter a,z; gtk_text_buffer_get_bounds(buffer,&a,&z);
    char *text = gtk_text_buffer_get_text(buffer,&a,&z,TRUE); assert(!strcmp(text,expected)); g_free(text);
}
static void navigation_checks(void) {
    const char *doc = "Before\n![a](a.png)\n![b](b.png)\nAfter";
    GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
    gtk_text_buffer_set_text(buffer,doc,-1);
    GPtrArray *refs = image_references(doc);
    ImageReference *first = g_ptr_array_index(refs,0), *last = g_ptr_array_index(refs,1);
    int after = last->end+1;
    assert(image_skip_position(buffer,refs,2,first->start,1) == after+2);
    assert(image_skip_position(buffer,refs,after+2,last->start,-1) == 2);
    assert(image_skip_position(buffer,refs,first->end,first->start,1) == after);
    assert(image_skip_position(buffer,refs,0,2,1) == -1);
    int back; char *text = image_boundary_text(buffer,refs,last->end,"body",4,&back);
    assert(text && !strcmp(text,"\nbody") && back == 0); g_free(text);
    text = image_boundary_text(buffer,refs,first->start,"title",5,&back);
    assert(text && !strcmp(text,"title\n") && back == 1); g_free(text);
    assert(!image_boundary_text(buffer,refs,last->end,"\n",1,&back));
    GtkTextIter at; gtk_text_buffer_get_iter_at_offset(buffer,&at,after);
    gtk_text_buffer_place_cursor(buffer,&at); assert(image_delete_edge(buffer,refs,TRUE));
    unchanged(buffer,"Before\n![a](a.png)\nAfter");
    g_ptr_array_unref(refs);
    gtk_text_buffer_set_text(buffer,"Before\n![a](a.png)\nAfter",-1);
    refs = image_references("Before\n![a](a.png)\nAfter");
    gtk_text_buffer_get_iter_at_offset(buffer,&at,6); gtk_text_buffer_place_cursor(buffer,&at);
    assert(image_delete_edge(buffer,refs,FALSE)); unchanged(buffer,"Before\nAfter");
    g_ptr_array_unref(refs);
    gtk_text_buffer_set_text(buffer,"![a](a.png)",-1); refs = image_references("![a](a.png)");
    assert(image_skip_position(buffer,refs,0,0,1) == gtk_text_buffer_get_char_count(buffer));
    assert(image_skip_position(buffer,refs,0,0,-1) == 0);
    gtk_text_buffer_get_end_iter(buffer,&at); gtk_text_buffer_place_cursor(buffer,&at);
    assert(image_delete_edge(buffer,refs,TRUE)); unchanged(buffer,"");
    g_ptr_array_unref(refs); g_object_unref(buffer);
}
static guint red_pixels(GdkPixbuf *image) {
    assert(image); guint count = 0; int channels = gdk_pixbuf_get_n_channels(image), stride = gdk_pixbuf_get_rowstride(image);
    for (int y = 0; y < gdk_pixbuf_get_height(image); y++) for (int x = 0; x < gdk_pixbuf_get_width(image); x++) {
        guchar *p = gdk_pixbuf_get_pixels(image)+y*stride+x*channels;
        if (p[0] > 240 && p[1] < 10 && p[2] < 10) count++;
    }
    return count;
}
int main(int argc, char **argv) {
    gboolean display = gtk_init_check(&argc,&argv); boxed_types(); navigation_checks();
    const char *source = "é🙂\n![first](assets/one.png) and ![second](assets/a(b).png)\n`![code](x.png)`\n```md\n![fenced](x.png)\n```\n\\![escaped](x.png)\n![space](<assets/two words.png>)";
    GPtrArray *refs = image_references(source); assert(refs->len == 3);
    ImageReference *ref = g_ptr_array_index(refs,0); assert(ref->start == 3 && !strcmp(ref->destination,"assets/one.png"));
    ref = g_ptr_array_index(refs,1); assert(!strcmp(ref->destination,"assets/a(b).png"));
    ref = g_ptr_array_index(refs,2); assert(!strcmp(ref->destination,"assets/two words.png")); g_ptr_array_unref(refs);
    refs = image_references("![unfinished](abc\n![complete](ok.png)"); assert(refs->len == 1); g_ptr_array_unref(refs);
    char *path = g_dir_make_tmp("writr-images-XXXXXX",NULL); assert(path);
    GFile *folder = g_file_new_for_path(path), *asset = NULL; GError *error = NULL;
    GdkPixbuf *original = gdk_pixbuf_new(GDK_COLORSPACE_RGB,TRUE,8,40,20); gdk_pixbuf_fill(original,0xff0000ff);
    char *markdown = paste_image(original,folder,&asset,&error); assert(markdown && !error);
    refs = image_references(markdown); assert(refs->len == 1); ref = g_ptr_array_index(refs,0);
    GFile *resolved = image_resolve(folder,ref->destination); assert(resolved && g_file_equal(resolved,asset)); g_object_unref(resolved);
    char *uri = g_file_get_uri(asset); resolved = image_resolve(NULL,uri); assert(g_file_equal(resolved,asset)); g_object_unref(resolved); g_free(uri);
    assert(!image_resolve(folder,"https://example.com/image.png"));
    resolved = image_resolve(folder,"assets/two%20words.png"); char *resolved_path = g_file_get_path(resolved); assert(g_str_has_suffix(resolved_path,"assets/two words.png")); g_free(resolved_path); g_object_unref(resolved);
    GdkPixbuf *decoded = image_read(asset,&error); assert(decoded && !error);
    assert(gdk_pixbuf_get_width(decoded) == 40 && gdk_pixbuf_get_height(decoded) == 20); assert(red_pixels(decoded) == 800); g_object_unref(decoded);
    GFile *bad = g_file_get_child(folder,"bad.png"); assert(g_file_replace_contents(bad,"invalid",7,NULL,FALSE,G_FILE_CREATE_NONE,NULL,NULL,&error));
    /* Missing-image errors are handled without changing the Markdown document. */
    GFile *missing = g_file_get_child(folder,"missing.png"); assert(!image_read(missing,&error)); assert(error); g_clear_error(&error); g_object_unref(missing);
    if (display) {
        GtkWidget *window = gtk_offscreen_window_new(), *view = gtk_text_view_new(); gtk_widget_set_size_request(view,480,360);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view),20); gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view),20);
        gtk_container_add(GTK_CONTAINER(window),view); GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)); markdown_init(buffer);
        char *document = g_strconcat("Before\n",markdown,"\nAfter",NULL); gtk_text_buffer_set_text(buffer,document,-1);
        ImageLayer *layer = images_new(GTK_TEXT_VIEW(view)); gtk_widget_show_all(window); markdown_style(buffer); images_refresh(layer,folder,FALSE);
        for (int i = 0; i < 50; i++) while (g_main_context_iteration(NULL,FALSE)) {}
        unchanged(buffer,document); GdkPixbuf *screenshot = gtk_offscreen_window_get_pixbuf(GTK_OFFSCREEN_WINDOW(window));
        assert(red_pixels(screenshot) >= 750); g_object_unref(screenshot);
        markdown_source(buffer,TRUE); images_refresh(layer,folder,TRUE);
        for (int i = 0; i < 50; i++) while (g_main_context_iteration(NULL,FALSE)) {}
        unchanged(buffer,document); screenshot = gtk_offscreen_window_get_pixbuf(GTK_OFFSCREEN_WINDOW(window)); assert(red_pixels(screenshot) < 100); g_object_unref(screenshot);
        markdown_source(buffer,FALSE); markdown_style(buffer); images_refresh(layer,folder,FALSE); unchanged(buffer,document);
        images_free(layer); gtk_widget_destroy(window); g_free(document);
        g_print("images: desktop rendering and source-mode checks passed\n");
    } else g_print("images: desktop rendering checks skipped (no display available)\n");
    assert(g_file_delete(asset,NULL,&error)); assert(g_file_delete(bad,NULL,&error));
    GFile *assets = g_file_get_child(folder,"assets"); assert(g_file_delete(assets,NULL,&error)); assert(g_file_delete(folder,NULL,&error));
    g_object_unref(assets); g_object_unref(bad); g_object_unref(asset); g_object_unref(folder); g_object_unref(original); g_free(path); g_free(markdown); g_ptr_array_unref(refs);
    g_print("images: source offsets, code exclusion, local paths and PNG decoding passed\n");
}
