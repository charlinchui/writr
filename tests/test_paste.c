#include "paste.h"
#include <glib/gstdio.h>
#include <assert.h>
#include <string.h>
#include <png.h>

static void check_link(const char *text, const char *label, const char *expected) {
    char *markdown = paste_link(text,label); assert(markdown && !strcmp(markdown,expected)); g_free(markdown);
}

int main(void) {
    check_link("https://example.com",NULL,"[https://example.com](https://example.com)");
    check_link(" https://example.com/a(b)?q=x%20y\n","Read [this]","[Read \\[this\\]](https://example.com/a%28b%29?q=x%20y)");
    check_link("http://example.com","Café 🙂","[Café 🙂](http://example.com)");
    assert(!paste_link("normal text",NULL)); assert(!paste_link("https://example.com\nsecond line",NULL));
    assert(!paste_link("javascript:alert(1)",NULL)); assert(!paste_link("file:///tmp/image.png",NULL));
    assert(!paste_link("https://",NULL)); assert(!paste_link("[label](https://example.com)",NULL));
    const char *source = "```c\nhttps://example.com\n"; assert(paste_code_at(source,strlen(source)));
    source = "~~~\ntext\n~~~ suffix\n"; assert(paste_code_at(source,strlen(source)));
    source = "```c\ntext\n```\nplain"; assert(!paste_code_at(source,strlen(source)));
    source = "`inline "; assert(paste_code_at(source,strlen(source)));
    source = "`inline` plain "; assert(!paste_code_at(source,strlen(source)));
    source = "``inline ` tick "; assert(paste_code_at(source,strlen(source)));
    source = "\\`literal "; assert(!paste_code_at(source,strlen(source)));
    source = "```c\n\n"; assert(paste_code_at(source,strlen(source)));
    char *path = g_dir_make_tmp("writr-paste-XXXXXX",NULL); assert(path);
    GFile *folder = g_file_new_for_path(path), *first = NULL, *second = NULL; GError *error = NULL;
    GdkPixbuf *image = gdk_pixbuf_new(GDK_COLORSPACE_RGB,TRUE,8,3,2); assert(image); gdk_pixbuf_fill(image,0x12345678);
    char *markdown = paste_image(image,folder,&first,&error); if (error) g_printerr("Image save failed: %s\n",error->message); assert(markdown && first && !error);
    char *name = g_file_get_basename(first), *expected = g_strdup_printf("![image](assets/%s)",name); assert(!strcmp(markdown,expected));
    g_free(markdown); g_free(expected); g_free(name);
    char *image_path = g_file_get_path(first); png_image decoded = {0}; decoded.version = PNG_IMAGE_VERSION;
    assert(png_image_begin_read_from_file(&decoded,image_path)); assert(decoded.width == 3 && decoded.height == 2);
    decoded.format = PNG_FORMAT_RGBA; unsigned char pixels[3*2*4];
    assert(png_image_finish_read(&decoded,NULL,pixels,0,NULL)); assert(!memcmp(pixels,"\x12\x34\x56\x78",4));
    png_image_free(&decoded); g_free(image_path);
    markdown = paste_image(image,folder,&second,&error); assert(markdown && second && !g_file_equal(first,second)); g_free(markdown);
    assert(g_file_delete(first,NULL,&error)); assert(g_file_delete(second,NULL,&error));
    GFile *assets = g_file_get_child(folder,"assets"); assert(g_file_delete(assets,NULL,&error));
    assert(g_file_replace_contents(assets,"blocked",7,NULL,FALSE,G_FILE_CREATE_NONE,NULL,NULL,&error));
    GFile *failed = NULL; assert(!paste_image(image,folder,&failed,&error)); assert(error && !failed); g_clear_error(&error);
    assert(g_file_delete(assets,NULL,&error)); assert(g_file_delete(folder,NULL,&error));
    g_object_unref(assets); g_object_unref(first); g_object_unref(second); g_object_unref(folder); g_object_unref(image); g_free(path);
    g_print("paste: URL conversion, escaping, code context, PNG round trips and asset failures passed\n");
}
