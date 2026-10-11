#ifndef WRITR_IMAGES_H
#define WRITR_IMAGES_H

#include <gtk/gtk.h>

typedef struct { int start, end; char *destination, *alt; } ImageReference;

typedef struct ImageLayer ImageLayer;

GPtrArray *image_references(const char *source);

GFile *image_resolve(GFile *folder, const char *destination);

GdkPixbuf *image_read(GFile *file, GError **error);

int image_skip_position(GtkTextBuffer *buffer, GPtrArray *refs, int from, int target, int direction);

gboolean image_delete_edge(GtkTextBuffer *buffer, GPtrArray *refs, gboolean backward);

char *image_boundary_text(GtkTextBuffer *buffer, GPtrArray *refs, int position, const char *text, int length, int *caret_back);

ImageLayer *images_new(GtkTextView *view);

void images_refresh(ImageLayer *layer, GFile *folder, gboolean source);

gboolean images_copy(ImageLayer *layer, gboolean cut);

void images_free(ImageLayer *layer);

#endif
