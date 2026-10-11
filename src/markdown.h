#ifndef WRITR_MARKDOWN_H
#define WRITR_MARKDOWN_H

#include <gtk/gtk.h>

void markdown_init(GtkTextBuffer *buffer);

void markdown_style(GtkTextBuffer *buffer);

void markdown_source(GtkTextBuffer *buffer, gboolean show);

#endif
