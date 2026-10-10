#ifndef WRITR_GAP_H
#define WRITR_GAP_H

#include <stddef.h>
#include <stdbool.h>

typedef struct { char *data; size_t capacity, start, end; } Gap;

size_t gap_length(const Gap *g);

bool gap_insert(Gap *g, size_t at, const char *text, size_t n);

bool gap_delete(Gap *g, size_t at, size_t n);

char *gap_text(const Gap *g);

void gap_free(Gap *g);

#endif
