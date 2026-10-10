#include "images.h"
#include "paste.h"
#include <png.h>
#include <string.h>
#include <limits.h>

static void reference_free(gpointer data) {
    ImageReference *ref = data; g_free(ref->destination); g_free(ref->alt); g_free(ref);
}
GPtrArray *image_references(const char *source) {
    GPtrArray *refs = g_ptr_array_new_with_free_func(reference_free);
    for (const char *p = source; *p; p = g_utf8_next_char(p)) {
        if (*p == '\\' && p[1]) { p = g_utf8_next_char(p); if (!*p) break; continue; }
        if (*p != '!' || p[1] != '[' || paste_code_at(source,(size_t)(p-source))) continue;
        const char *label = p+2, *end = label;
        while (*end && *end != '\n' && !(*end == ']' && end[1] == '(')) { if (*end == '\\' && end[1]) end++; end++; }
        if (!*end || *end == '\n') continue;
        const char *url = end+2, *close = url; int depth = 1; gboolean angle = *url == '<';
        if (angle) { close = strchr(url+1,'>'); if (!close || close[1] != ')') continue; close++; }
        else {
            while (*close && *close != '\n') {
                if (*close == '\\' && close[1]) { close += 2; continue; }
                if (*close == '(') depth++;
                if (*close == ')') { depth--; if (!depth) break; }
                close++;
            }
            if (!*close || *close == '\n') continue;
        }
        char *destination = g_strndup(url+(angle ? 1 : 0),(gsize)(close-url)-(angle ? 2 : 0)); g_strstrip(destination);
        if (!*destination) { g_free(destination); continue; }
        ImageReference *ref = g_new0(ImageReference,1);
        ref->start = (int)g_utf8_strlen(source,p-source); ref->end = (int)g_utf8_strlen(source,close-source+1);
        ref->destination = destination; ref->alt = g_strndup(label,end-label); g_ptr_array_add(refs,ref);
        p = close;
    }
    return refs;
}
GFile *image_resolve(GFile *folder, const char *destination) {
    char *scheme = g_uri_parse_scheme(destination); GFile *file = NULL;
    if (scheme) { if (!g_ascii_strcasecmp(scheme,"file")) file = g_file_new_for_uri(destination); }
    else {
        char *path = g_uri_unescape_string(destination,NULL);
        if (path) {
            if (g_path_is_absolute(path)) file = g_file_new_for_path(path);
            else if (folder) file = g_file_resolve_relative_path(folder,path);
            g_free(path);
        }
    }
    g_free(scheme); return file;
}
static void pixels_free(guchar *pixels, gpointer data) { (void)data; g_free(pixels); }
GdkPixbuf *image_read(GFile *file, GError **error) {
    char *bytes = NULL; gsize size = 0;
    if (!g_file_load_contents(file,NULL,&bytes,&size,NULL,error)) return NULL;
    if (size >= 8 && !png_sig_cmp((png_const_bytep)bytes,0,8)) {
        png_image png = {0}; png.version = PNG_IMAGE_VERSION;
        if (!png_image_begin_read_from_memory(&png,bytes,size)) {
            g_set_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Cannot read image: %s",png.message); png_image_free(&png); g_free(bytes); return NULL;
        }
        png.format = PNG_FORMAT_RGBA;
        if (!png.width || !png.height || png.width > INT_MAX/4 || png.height > INT_MAX || (guint64)png.width*png.height > 32*1024*1024) {
            g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Image is too large to preview."); png_image_free(&png); g_free(bytes); return NULL;
        }
        guchar *pixels = g_try_malloc(PNG_IMAGE_SIZE(png));
        if (!pixels || !png_image_finish_read(&png,NULL,pixels,0,NULL)) {
            g_set_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Cannot decode image: %s",pixels ? png.message : "not enough memory");
            g_free(pixels); png_image_free(&png); g_free(bytes); return NULL;
        }
        GdkPixbuf *result = gdk_pixbuf_new_from_data(pixels,GDK_COLORSPACE_RGB,TRUE,8,(int)png.width,(int)png.height,(int)png.width*4,pixels_free,NULL);
        png_image_free(&png); g_free(bytes); return result;
    }
    GInputStream *stream = g_memory_input_stream_new_from_data(bytes,(gssize)size,g_free);
    GdkPixbuf *result = gdk_pixbuf_new_from_stream(stream,NULL,error); g_object_unref(stream); return result;
}

/* The preview is an atomic block; raw source mode keeps normal character editing. */
static gboolean image_line_only(GtkTextBuffer *buffer, GPtrArray *refs, int line) {
    GtkTextIter at; gtk_text_buffer_get_iter_at_line(buffer,&at,line); gboolean found = FALSE;
    while (!gtk_text_iter_ends_line(&at)) {
        int position = gtk_text_iter_get_offset(&at); gboolean image = FALSE;
        for (guint i = 0; i < refs->len; i++) {
            ImageReference *ref = g_ptr_array_index(refs,i);
            if (position >= ref->start && position < ref->end) {
                gtk_text_buffer_get_iter_at_offset(buffer,&at,ref->end); found = image = TRUE; break;
            }
        }
        if (image) continue;
        if (!g_unichar_isspace(gtk_text_iter_get_char(&at))) return FALSE;
        gtk_text_iter_forward_char(&at);
    }
    return found;
}
int image_skip_position(GtkTextBuffer *buffer, GPtrArray *refs, int from, int target, int direction) {
    GtkTextIter at,next; gtk_text_buffer_get_iter_at_offset(buffer,&at,from); gtk_text_buffer_get_iter_at_offset(buffer,&next,target);
    int line = gtk_text_iter_get_line(&next), current = gtk_text_iter_get_line(&at), column = gtk_text_iter_get_line_offset(&at);
    gboolean current_image = image_line_only(buffer,refs,current);
    if (current_image) { line = current+direction; column = 0; }
    else if (!image_line_only(buffer,refs,line)) return -1;
    int lines = gtk_text_buffer_get_line_count(buffer);
    while (line >= 0 && line < lines && image_line_only(buffer,refs,line)) line += direction;
    if (line < 0) return 0;
    if (line >= lines) return gtk_text_buffer_get_char_count(buffer);
    gtk_text_buffer_get_iter_at_line(buffer,&next,line);
    while (column-- > 0 && !gtk_text_iter_ends_line(&next)) gtk_text_iter_forward_char(&next);
    return gtk_text_iter_get_offset(&next);
}
gboolean image_delete_edge(GtkTextBuffer *buffer, GPtrArray *refs, gboolean backward) {
    if (gtk_text_buffer_get_has_selection(buffer)) return FALSE;
    GtkTextIter at; gtk_text_buffer_get_iter_at_mark(buffer,&at,gtk_text_buffer_get_insert(buffer)); int position = gtk_text_iter_get_offset(&at);
    for (guint i = 0; i < refs->len; i++) {
        ImageReference *ref = g_ptr_array_index(refs,i); GtkTextIter a,z;
        gtk_text_buffer_get_iter_at_offset(buffer,&a,ref->start); gtk_text_buffer_get_iter_at_offset(buffer,&z,ref->end);
        gboolean newline = gtk_text_iter_get_char(&z) == '\n', standalone = image_line_only(buffer,refs,gtk_text_iter_get_line(&a));
        gboolean adjacent = backward ? (position == ref->end || (standalone && newline && position == ref->end+1)) :
            (position == ref->start || (standalone && gtk_text_iter_get_char(&at) == '\n' && position+1 == ref->start));
        if (!adjacent) continue;
        if (standalone && newline && (!backward || position == ref->end+1)) gtk_text_iter_forward_char(&z);
        gtk_text_buffer_begin_user_action(buffer); gtk_text_buffer_delete(buffer,&a,&z);
        /* Keep the cursor in the surviving text when deleting a following image. */
        gtk_text_buffer_get_iter_at_offset(buffer,&at,MIN(position,gtk_text_iter_get_offset(&a)));
        gtk_text_buffer_place_cursor(buffer,&at); gtk_text_buffer_end_user_action(buffer); return TRUE;
    }
    return FALSE;
}
char *image_boundary_text(GtkTextBuffer *buffer, GPtrArray *refs, int position, const char *text, int length, int *caret_back) {
    *caret_back = 0; if (length <= 0) return NULL;
    for (guint i = 0; i < refs->len; i++) {
        ImageReference *ref = g_ptr_array_index(refs,i); GtkTextIter at; gtk_text_buffer_get_iter_at_offset(buffer,&at,ref->start);
        if (!image_line_only(buffer,refs,gtk_text_iter_get_line(&at))) continue;
        char *copy = NULL, *result = NULL;
        if (position == ref->end && text[0] != '\n') { copy = g_strndup(text,(gsize)length); result = g_strconcat("\n",copy,NULL); }
        else if (position == ref->start && text[length-1] != '\n') { copy = g_strndup(text,(gsize)length); result = g_strconcat(copy,"\n",NULL); *caret_back = 1; }
        g_free(copy); if (result) return result;
    }
    return NULL;
}

typedef struct { GtkTextMark *start; GtkTextTag *space; int height; } ImageBlock;
typedef struct { GtkTextMark *start, *end; ImageBlock *block; GdkPixbuf *original, *scaled; int offset; } Preview;
struct ImageLayer {
    GtkTextView *view; GtkTextBuffer *buffer; GtkTextTag *hidden;
    GPtrArray *previews, *blocks; GHashTable *cache;
    gulong draw_handler, size_handler, press_handler, mark_handler, key_handler, insert_handler, inserted_handler;
    gboolean inserting;
    guint layout_source; int width;
};
static void clear(ImageLayer *layer) {
    for (guint i = 0; i < layer->previews->len; i++) {
        Preview *preview = g_ptr_array_index(layer->previews,i);
        gtk_text_buffer_delete_mark(layer->buffer,preview->start); gtk_text_buffer_delete_mark(layer->buffer,preview->end);
        g_clear_object(&preview->original); g_clear_object(&preview->scaled); g_free(preview);
    }
    g_ptr_array_set_size(layer->previews,0);
    for (guint i = 0; i < layer->blocks->len; i++) {
        ImageBlock *block = g_ptr_array_index(layer->blocks,i);
        gtk_text_buffer_delete_mark(layer->buffer,block->start);
        gtk_text_tag_table_remove(gtk_text_buffer_get_tag_table(layer->buffer),block->space); g_object_unref(block->space); g_free(block);
    }
    g_ptr_array_set_size(layer->blocks,0);
    GtkTextIter a,z; gtk_text_buffer_get_bounds(layer->buffer,&a,&z); gtk_text_buffer_remove_tag(layer->buffer,layer->hidden,&a,&z);
}
static void layout(ImageLayer *layer) {
    int width = MAX(1,gtk_widget_get_allocated_width(GTK_WIDGET(layer->view))-gtk_text_view_get_left_margin(layer->view)-gtk_text_view_get_right_margin(layer->view)-8);
    layer->width = width;
    for (guint i = 0; i < layer->blocks->len; i++) ((ImageBlock *)g_ptr_array_index(layer->blocks,i))->height = 0;
    for (guint i = 0; i < layer->previews->len; i++) {
        Preview *preview = g_ptr_array_index(layer->previews,i);
        int w = gdk_pixbuf_get_width(preview->original), h = gdk_pixbuf_get_height(preview->original);
        double ratio = MIN(1.0,MIN((double)width/w,600.0/h));
        int sw = MAX(1,(int)(w*ratio)), sh = MAX(1,(int)(h*ratio));
        g_clear_object(&preview->scaled);
        preview->scaled = ratio == 1.0 ? g_object_ref(preview->original) : gdk_pixbuf_scale_simple(preview->original,sw,sh,GDK_INTERP_BILINEAR);
        preview->offset = preview->block->height; preview->block->height += sh+12;
    }
    for (guint i = 0; i < layer->blocks->len; i++) {
        ImageBlock *block = g_ptr_array_index(layer->blocks,i);
        g_object_set(block->space,"pixels-above-lines",block->height,NULL);
    }
    gtk_widget_queue_draw(GTK_WIDGET(layer->view));
}
static gboolean resize_idle(gpointer data) { ImageLayer *layer = data; layer->layout_source = 0; layout(layer); return G_SOURCE_REMOVE; }
static void resized(GtkWidget *widget, GtkAllocation *allocation, ImageLayer *layer) {
    (void)widget;
    int width = MAX(1,allocation->width-gtk_text_view_get_left_margin(layer->view)-gtk_text_view_get_right_margin(layer->view)-8);
    if (width != layer->width && !layer->layout_source) layer->layout_source = g_idle_add(resize_idle,layer);
}
static gboolean rectangle(ImageLayer *layer, Preview *preview, GdkRectangle *rect) {
    GtkTextIter a,z; gtk_text_buffer_get_iter_at_mark(layer->buffer,&a,preview->start); gtk_text_buffer_get_iter_at_mark(layer->buffer,&z,preview->end);
    if (gtk_text_iter_compare(&a,&z) >= 0 || !preview->scaled) return FALSE;
    GtkTextIter line; gtk_text_buffer_get_iter_at_mark(layer->buffer,&line,preview->block->start);
    int y,height; gtk_text_view_get_line_yrange(layer->view,&line,&y,&height);
    gtk_text_view_buffer_to_window_coords(layer->view,GTK_TEXT_WINDOW_TEXT,gtk_text_view_get_left_margin(layer->view),y+preview->offset+6,&rect->x,&rect->y);
    rect->width = gdk_pixbuf_get_width(preview->scaled); rect->height = gdk_pixbuf_get_height(preview->scaled); return TRUE;
}
static gboolean selected(ImageLayer *layer, Preview *preview) {
    GtkTextIter a,z,start,end;
    if (!gtk_text_buffer_get_selection_bounds(layer->buffer,&a,&z)) return FALSE;
    gtk_text_buffer_get_iter_at_mark(layer->buffer,&start,preview->start); gtk_text_buffer_get_iter_at_mark(layer->buffer,&end,preview->end);
    return gtk_text_iter_compare(&a,&end) < 0 && gtk_text_iter_compare(&z,&start) > 0;
}
static gboolean draw(GtkWidget *widget, cairo_t *cr, ImageLayer *layer) {
    GdkWindow *window = gtk_text_view_get_window(layer->view,GTK_TEXT_WINDOW_TEXT);
    if (!window || !gtk_cairo_should_draw_window(cr,window)) return FALSE;
    cairo_save(cr); gtk_cairo_transform_to_window(cr,widget,window);
    cairo_rectangle(cr,0,0,gdk_window_get_width(window),gdk_window_get_height(window)); cairo_clip(cr);
    for (guint i = 0; i < layer->previews->len; i++) {
        Preview *preview = g_ptr_array_index(layer->previews,i); GdkRectangle rect;
        if (!rectangle(layer,preview,&rect)) continue;
        gdk_cairo_set_source_pixbuf(cr,preview->scaled,rect.x,rect.y); cairo_paint(cr);
        if (selected(layer,preview)) {
            cairo_set_source_rgb(cr,.35,.6,1); cairo_set_line_width(cr,2); cairo_rectangle(cr,rect.x,rect.y,rect.width,rect.height); cairo_stroke(cr);
        }
    }
    cairo_restore(cr); return FALSE;
}
static GPtrArray *current_refs(ImageLayer *layer);
static gboolean pressed(GtkWidget *widget, GdkEventButton *event, ImageLayer *layer) {
    if (event->button != 1 || gtk_text_view_get_window_type(layer->view,event->window) != GTK_TEXT_WINDOW_TEXT) return FALSE;
    for (guint i = 0; i < layer->previews->len; i++) {
        Preview *preview = g_ptr_array_index(layer->previews,i); GdkRectangle rect;
        if (!rectangle(layer,preview,&rect)) continue;
        if (event->x >= rect.x && event->x < rect.x+rect.width && event->y >= rect.y && event->y < rect.y+rect.height) {
            GtkTextIter a,z; gtk_text_buffer_get_iter_at_mark(layer->buffer,&a,preview->start); gtk_text_buffer_get_iter_at_mark(layer->buffer,&z,preview->end);
            gtk_text_buffer_select_range(layer->buffer,&z,&a); gtk_widget_grab_focus(widget); gtk_widget_queue_draw(widget); return TRUE;
        }
    }
    /* The text row under a picture is a usable insertion point, including at EOF. */
    int bx,by; GtkTextIter hit;
    gtk_text_view_window_to_buffer_coords(layer->view,GTK_TEXT_WINDOW_TEXT,(int)event->x,(int)event->y,&bx,&by);
    gtk_text_view_get_iter_at_location(layer->view,&hit,bx,by);
    for (guint i = layer->previews->len; i > 0; i--) {
        Preview *preview = g_ptr_array_index(layer->previews,i-1); GdkRectangle rect; GtkTextIter start,end;
        if (!rectangle(layer,preview,&rect) || event->y < rect.y+rect.height) continue;
        gtk_text_buffer_get_iter_at_mark(layer->buffer,&start,preview->start); gtk_text_buffer_get_iter_at_mark(layer->buffer,&end,preview->end);
        if (gtk_text_iter_get_line(&hit) != gtk_text_iter_get_line(&start)) continue;
        GPtrArray *refs = current_refs(layer);
        gboolean standalone = image_line_only(layer->buffer,refs,gtk_text_iter_get_line(&start));
        g_ptr_array_unref(refs); if (!standalone) continue;
        gtk_text_buffer_place_cursor(layer->buffer,&end); gtk_widget_grab_focus(widget); return TRUE;
    }
    return FALSE;
}
static void mark_set(GtkTextBuffer *buffer, GtkTextIter *at, GtkTextMark *mark, ImageLayer *layer) {
    (void)buffer; (void)at; (void)mark; gtk_widget_queue_draw(GTK_WIDGET(layer->view));
}
static GPtrArray *current_refs(ImageLayer *layer) {
    GPtrArray *refs = g_ptr_array_new_with_free_func(reference_free);
    for (guint i = 0; i < layer->previews->len; i++) {
        Preview *preview = g_ptr_array_index(layer->previews,i); GtkTextIter a,z;
        gtk_text_buffer_get_iter_at_mark(layer->buffer,&a,preview->start); gtk_text_buffer_get_iter_at_mark(layer->buffer,&z,preview->end);
        if (gtk_text_iter_compare(&a,&z) >= 0) continue;
        ImageReference *ref = g_new0(ImageReference,1); ref->start = gtk_text_iter_get_offset(&a); ref->end = gtk_text_iter_get_offset(&z); g_ptr_array_add(refs,ref);
    }
    return refs;
}
static gboolean image_key(GtkWidget *widget, GdkEventKey *event, ImageLayer *layer) {
    if (event->state & (GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SUPER_MASK)) return FALSE;
    gboolean up = event->keyval == GDK_KEY_Up, down = event->keyval == GDK_KEY_Down;
    gboolean backward = event->keyval == GDK_KEY_BackSpace, forward = event->keyval == GDK_KEY_Delete;
    if (!(up || down || backward || forward)) return FALSE;
    GPtrArray *refs = current_refs(layer); gboolean handled = FALSE;
    if ((backward || forward) && !(event->state & GDK_SHIFT_MASK)) handled = image_delete_edge(layer->buffer,refs,backward);
    else if (up || down) {
        GtkTextIter at,next,anchor; gtk_text_buffer_get_iter_at_mark(layer->buffer,&at,gtk_text_buffer_get_insert(layer->buffer)); next = at;
        if (up) gtk_text_view_backward_display_line(layer->view,&next); else gtk_text_view_forward_display_line(layer->view,&next);
        int position = image_skip_position(layer->buffer,refs,gtk_text_iter_get_offset(&at),gtk_text_iter_get_offset(&next),up ? -1 : 1);
        if (position >= 0) {
            gtk_text_buffer_get_iter_at_offset(layer->buffer,&next,position);
            if (event->state & GDK_SHIFT_MASK) {
                gtk_text_buffer_get_iter_at_mark(layer->buffer,&anchor,gtk_text_buffer_get_selection_bound(layer->buffer)); gtk_text_buffer_select_range(layer->buffer,&next,&anchor);
            } else gtk_text_buffer_place_cursor(layer->buffer,&next);
            handled = TRUE;
        }
    }
    g_ptr_array_unref(refs);
    if (handled) { gtk_text_view_scroll_mark_onscreen(layer->view,gtk_text_buffer_get_insert(layer->buffer)); gtk_widget_queue_draw(widget); }
    return handled;
}
static void boundary_insert(GtkTextBuffer *buffer, GtkTextIter *location, char *text, int length, ImageLayer *layer) {
    if (layer->inserting || !layer->previews->len) return;
    GPtrArray *refs = current_refs(layer); int back = 0;
    char *replacement = image_boundary_text(buffer,refs,gtk_text_iter_get_offset(location),text,length,&back); g_ptr_array_unref(refs);
    if (!replacement) return;
    g_signal_stop_emission_by_name(buffer,"insert-text");
    layer->inserting = TRUE; GtkTextIter at = *location;
    gtk_text_buffer_begin_user_action(buffer); gtk_text_buffer_insert(buffer,&at,replacement,-1);
    if (back) { gtk_text_iter_backward_char(&at); gtk_text_buffer_place_cursor(buffer,&at); }
    *location = at; gtk_text_buffer_end_user_action(buffer); layer->inserting = FALSE; g_free(replacement);
}
static void boundary_inserted(GtkTextBuffer *buffer, GtkTextIter *location, char *text, int length, ImageLayer *layer) {
    if (!layer->previews->len) return;
    GtkTextIter start = *location; gtk_text_iter_backward_chars(&start,(int)g_utf8_strlen(text,length));
    /* Text added at a tag boundary must not inherit an image's invisibility or spacing. */
    gtk_text_buffer_remove_tag(buffer,layer->hidden,&start,location);
    GtkTextTag *syntax = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer),"syntax");
    if (syntax) gtk_text_buffer_remove_tag(buffer,syntax,&start,location);
    for (guint i = 0; i < layer->blocks->len; i++) {
        ImageBlock *block = g_ptr_array_index(layer->blocks,i); gtk_text_buffer_remove_tag(buffer,block->space,&start,location);
    }
}
ImageLayer *images_new(GtkTextView *view) {
    ImageLayer *layer = g_new0(ImageLayer,1); layer->view = g_object_ref(view); layer->buffer = g_object_ref(gtk_text_view_get_buffer(view));
    layer->hidden = gtk_text_buffer_create_tag(layer->buffer,NULL,"invisible",TRUE,NULL);
    layer->previews = g_ptr_array_new(); layer->blocks = g_ptr_array_new();
    layer->cache = g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_object_unref);
    layer->draw_handler = g_signal_connect_after(view,"draw",G_CALLBACK(draw),layer);
    layer->size_handler = g_signal_connect(view,"size-allocate",G_CALLBACK(resized),layer);
    layer->press_handler = g_signal_connect(view,"button-press-event",G_CALLBACK(pressed),layer);
    layer->mark_handler = g_signal_connect(layer->buffer,"mark-set",G_CALLBACK(mark_set),layer);
    layer->key_handler = g_signal_connect(view,"key-press-event",G_CALLBACK(image_key),layer);
    layer->insert_handler = g_signal_connect(layer->buffer,"insert-text",G_CALLBACK(boundary_insert),layer);
    layer->inserted_handler = g_signal_connect_after(layer->buffer,"insert-text",G_CALLBACK(boundary_inserted),layer);
    return layer;
}
void images_refresh(ImageLayer *layer, GFile *folder, gboolean source) {
    if (!layer) return;
    clear(layer);
    if (source) { g_hash_table_remove_all(layer->cache); gtk_widget_queue_draw(GTK_WIDGET(layer->view)); return; }
    GHashTable *cache = g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_object_unref);
    GtkTextIter a,z; gtk_text_buffer_get_bounds(layer->buffer,&a,&z);
    char *text = gtk_text_buffer_get_text(layer->buffer,&a,&z,TRUE); GPtrArray *refs = image_references(text); g_free(text);
    int previous_line = -1; ImageBlock *block = NULL;
    for (guint i = 0; i < refs->len; i++) {
        ImageReference *ref = g_ptr_array_index(refs,i); GFile *file = image_resolve(folder,ref->destination);
        if (!file) continue;
        char *uri = g_file_get_uri(file); GFileInfo *info = g_file_query_info(file,"etag::value",G_FILE_QUERY_INFO_NONE,NULL,NULL);
        char *key = g_strdup_printf("%s:%s",uri,info && g_file_info_get_etag(info) ? g_file_info_get_etag(info) : "");
        g_free(uri); g_clear_object(&info);
        GdkPixbuf *image = g_hash_table_lookup(cache,key);
        if (!image) {
            image = g_hash_table_lookup(layer->cache,key);
            image = image ? g_object_ref(image) : image_read(file,NULL);
            if (image) g_hash_table_insert(cache,g_strdup(key),image);
        }
        g_free(key); g_object_unref(file); if (!image) continue;
        gtk_text_buffer_get_iter_at_offset(layer->buffer,&a,ref->start); gtk_text_buffer_get_iter_at_offset(layer->buffer,&z,ref->end);
        int line = gtk_text_iter_get_line(&a);
        if (line != previous_line) {
            block = g_new0(ImageBlock,1); GtkTextIter start = a,end = a; gtk_text_iter_set_line_offset(&start,0); gtk_text_iter_forward_line(&end);
            block->start = gtk_text_buffer_create_mark(layer->buffer,NULL,&start,FALSE);
            block->space = gtk_text_buffer_create_tag(layer->buffer,NULL,"pixels-above-lines",1,NULL); g_object_ref(block->space);
            gtk_text_buffer_apply_tag(layer->buffer,block->space,&start,&end); g_ptr_array_add(layer->blocks,block); previous_line = line;
        }
        Preview *preview = g_new0(Preview,1); preview->block = block; preview->original = g_object_ref(image);
        preview->start = gtk_text_buffer_create_mark(layer->buffer,NULL,&a,FALSE); preview->end = gtk_text_buffer_create_mark(layer->buffer,NULL,&z,TRUE);
        gtk_text_buffer_apply_tag(layer->buffer,layer->hidden,&a,&z); g_ptr_array_add(layer->previews,preview);
    }
    g_ptr_array_unref(refs); g_hash_table_unref(layer->cache); layer->cache = cache; layout(layer);
}
gboolean images_copy(ImageLayer *layer, gboolean cut) {
    if (!layer) return FALSE;
    GtkTextIter a,z;
    if (!gtk_text_buffer_get_selection_bounds(layer->buffer,&a,&z)) return FALSE;
    for (guint i = 0; i < layer->previews->len; i++) {
        Preview *preview = g_ptr_array_index(layer->previews,i); GtkTextIter start,end;
        gtk_text_buffer_get_iter_at_mark(layer->buffer,&start,preview->start); gtk_text_buffer_get_iter_at_mark(layer->buffer,&end,preview->end);
        if (!gtk_text_iter_equal(&a,&start) || !gtk_text_iter_equal(&z,&end)) continue;
        GtkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(layer->view),GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_image(clipboard,preview->original);
        if (cut) { gtk_text_buffer_begin_user_action(layer->buffer); gtk_text_buffer_delete(layer->buffer,&a,&z); gtk_text_buffer_end_user_action(layer->buffer); }
        return TRUE;
    }
    return FALSE;
}
void images_free(ImageLayer *layer) {
    if (!layer) return;
    if (layer->layout_source) g_source_remove(layer->layout_source);
    g_signal_handler_disconnect(layer->view,layer->draw_handler); g_signal_handler_disconnect(layer->view,layer->size_handler); g_signal_handler_disconnect(layer->view,layer->press_handler);
    g_signal_handler_disconnect(layer->buffer,layer->mark_handler);
    g_signal_handler_disconnect(layer->view,layer->key_handler); g_signal_handler_disconnect(layer->buffer,layer->insert_handler); g_signal_handler_disconnect(layer->buffer,layer->inserted_handler);
    clear(layer); gtk_text_tag_table_remove(gtk_text_buffer_get_tag_table(layer->buffer),layer->hidden);
    g_ptr_array_unref(layer->previews); g_ptr_array_unref(layer->blocks); g_hash_table_unref(layer->cache); g_object_unref(layer->view); g_object_unref(layer->buffer); g_free(layer);
}
