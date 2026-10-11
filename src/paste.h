#ifndef WRITR_PASTE_H
#define WRITR_PASTE_H

#include <gtk/gtk.h>

char *paste_link(const char *text, const char *label);

char *paste_image(GdkPixbuf *image, GFile *folder, GFile **asset, GError **error);

gboolean paste_code_at(const char *source, size_t position);

#endif
