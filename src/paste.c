#include "paste.h"
#include <string.h>
#include <png.h>

static char *escape_label(const char *text) {
    GString *label = g_string_new(NULL);
    for (const char *p = text; *p; p++) {
        if (*p == '\n' || *p == '\r') { g_string_append_c(label,' '); continue; }
        if (strchr("\\[]*_`",*p)) g_string_append_c(label,'\\');
        g_string_append_c(label,*p);
    }
    return g_string_free(label,FALSE);
}
char *paste_link(const char *text, const char *label) {
    if (!text || !g_utf8_validate(text,-1,NULL)) return NULL;
    char *url = g_strdup(text); g_strstrip(url);
    for (const char *p = url; *p; p = g_utf8_next_char(p)) if (g_unichar_isspace(g_utf8_get_char(p))) { g_free(url); return NULL; }
    GUri *uri = g_uri_parse(url,G_URI_FLAGS_ENCODED,NULL);
    const char *scheme = uri ? g_uri_get_scheme(uri) : NULL, *host = uri ? g_uri_get_host(uri) : NULL;
    gboolean valid = scheme && (!g_ascii_strcasecmp(scheme,"http") || !g_ascii_strcasecmp(scheme,"https")) && host && *host;
    if (uri) g_uri_unref(uri);
    if (!valid) { g_free(url); return NULL; }
    char *title = escape_label(label && *label ? label : url);
    /* Escape delimiters without double-encoding existing URL escapes. */
    GString *destination = g_string_new(NULL);
    for (const unsigned char *p = (const unsigned char *)url; *p; p++) {
        if (*p < 0x20 || strchr("()<>\\\"",*p)) g_string_append_printf(destination,"%%%02X",*p);
        else g_string_append_c(destination,(char)*p);
    }
    char *markdown = g_strdup_printf("[%s](%s)",title,destination->str);
    g_free(title); g_free(url); g_string_free(destination,TRUE); return markdown;
}
static gboolean encode_png(GdkPixbuf *image, char **buffer, gsize *size, GError **error) {
    png_image png = {0}; png.version = PNG_IMAGE_VERSION;
    png.width = (png_uint_32)gdk_pixbuf_get_width(image); png.height = (png_uint_32)gdk_pixbuf_get_height(image);
    png.format = gdk_pixbuf_get_has_alpha(image) ? PNG_FORMAT_RGBA : PNG_FORMAT_RGB;
    png_alloc_size_t bytes = 0;
    if (!png_image_write_to_memory(&png,NULL,&bytes,0,gdk_pixbuf_get_pixels(image),gdk_pixbuf_get_rowstride(image),NULL)) {
        g_set_error(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot encode image: %s",png.message); png_image_free(&png); return FALSE;
    }
    *buffer = g_try_malloc(bytes);
    if (!*buffer) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Not enough memory to save the image."); png_image_free(&png); return FALSE; }
    gboolean ok = png_image_write_to_memory(&png,*buffer,&bytes,0,gdk_pixbuf_get_pixels(image),gdk_pixbuf_get_rowstride(image),NULL);
    if (!ok) { g_set_error(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot encode image: %s",png.message); g_clear_pointer(buffer,g_free); }
    else *size = bytes;
    png_image_free(&png); return ok;
}
char *paste_image(GdkPixbuf *image, GFile *folder, GFile **asset, GError **error) {
    *asset = NULL;
    if (!folder) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_FOUND,"Create a workspace or save this note before pasting an image."); return NULL; }
    char *png = NULL; gsize size = 0;
    if (!encode_png(image,&png,&size,error)) return NULL;
    GFile *assets = g_file_get_child(folder,"assets");
    GError *local = NULL;
    if (!g_file_make_directory_with_parents(assets,NULL,&local)) {
        if (!g_error_matches(local,G_IO_ERROR,G_IO_ERROR_EXISTS)) {
            g_propagate_error(error,local); g_object_unref(assets); g_free(png); return NULL;
        }
        g_clear_error(&local);
    }
    char *uuid = g_uuid_string_random(), *name = g_strdup_printf("pasted-%s.png",uuid); g_free(uuid);
    GFile *file = g_file_get_child(assets,name); g_object_unref(assets);
    GFileOutputStream *stream = g_file_create(file,G_FILE_CREATE_NONE,NULL,error);
    if (!stream) { g_object_unref(file); g_free(name); g_free(png); return NULL; }
    gboolean ok = g_output_stream_write_all(G_OUTPUT_STREAM(stream),png,size,NULL,NULL,error);
    if (ok) ok = g_output_stream_close(G_OUTPUT_STREAM(stream),NULL,error);
    else g_output_stream_close(G_OUTPUT_STREAM(stream),NULL,NULL);
    g_object_unref(stream); g_free(png);
    char *markdown = NULL;
    if (ok) { markdown = g_strdup_printf("![image](assets/%s)",name); *asset = file; }
    else { g_file_delete(file,NULL,NULL); g_object_unref(file); }
    g_free(name); return markdown;
}
/* Read the source, not potentially stale display tags, to avoid converting code URLs. */
gboolean paste_code_at(const char *source, size_t position) {
    size_t length = strlen(source); if (position > length) position = length;
    char fence = 0; size_t fence_length = 0, inline_length = 0;
    for (size_t line = 0; line <= position;) {
        size_t end = line; while (end < length && source[end] != '\n') end++;
        size_t indent = line; while (indent < end && indent-line < 3 && source[indent] == ' ') indent++;
        char c = source[indent]; size_t run = 0;
        if (c == '`' || c == '~') while (indent+run < end && source[indent+run] == c) run++;
        gboolean closing = fence && c == fence && run >= fence_length;
        if (closing) for (size_t i = indent+run; i < end; i++) if (!g_ascii_isspace(source[i])) closing = FALSE;
        if (closing) { if (position <= end) return TRUE; fence = 0; }
        else if (fence) { if (position <= end) return TRUE; }
        else if (run >= 3) { if (position <= end) return TRUE; fence = c; fence_length = run; }
        else {
            for (size_t i = line; i < end && i < position;) {
                if (source[i] == '\\' && !inline_length && i+1 < end) { i += 2; continue; }
                if (source[i] != '`') { i++; continue; }
                size_t width = 1; while (i+width < end && source[i+width] == '`') width++;
                if (i+width > position) return TRUE;
                if (!inline_length) inline_length = width;
                else if (inline_length == width) inline_length = 0;
                i += width;
            }
            if (position <= end) return inline_length != 0;
        }
        if (end == length) break;
        line = end+1;
    }
    return fence != 0 || inline_length != 0;
}
