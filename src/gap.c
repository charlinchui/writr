#include "gap.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

size_t gap_length(const Gap *g) { return g->capacity - (g->end - g->start); }

static void move(Gap *g, size_t at) {
    if (at < g->start) {
        size_t n = g->start - at;
        memmove(g->data + g->end - n, g->data + at, n);
        g->start -= n; g->end -= n;
    } else if (at > g->start) {
        size_t n = at - g->start;
        memmove(g->data + g->start, g->data + g->end, n);
        g->start += n; g->end += n;
    }
}

bool gap_insert(Gap *g, size_t at, const char *text, size_t n) {
    size_t len = gap_length(g);
    if (at > len || n > SIZE_MAX - len - 1) return false;
    if (!n) return true;
    if (g->end - g->start < n) {
        size_t need = len + n + 1, cap = g->capacity ? g->capacity : 256;
        while (cap < need) { if (cap > SIZE_MAX / 2) { cap = need; break; } cap *= 2; }
        char *p = malloc(cap);
        if (!p) return false;
        size_t tail = g->capacity - g->end;
        if (g->start) memcpy(p, g->data, g->start);
        if (tail) memcpy(p + cap - tail, g->data + g->end, tail);
        free(g->data); g->data = p; g->capacity = cap; g->end = cap - tail;
    }
    move(g, at); memcpy(g->data + g->start, text, n); g->start += n;
    return true;
}

bool gap_delete(Gap *g, size_t at, size_t n) {
    size_t len = gap_length(g);
    if (at > len || n > len - at) return false;
    if (n) { move(g, at); g->end += n; }
    return true;
}

char *gap_text(const Gap *g) {
    size_t len = gap_length(g); char *p = malloc(len + 1);
    if (!p) return NULL;
    if (g->start) memcpy(p, g->data, g->start);
    if (g->capacity > g->end) memcpy(p + g->start, g->data + g->end, g->capacity - g->end);
    p[len] = 0; return p;
}

void gap_free(Gap *g) { free(g->data); *g = (Gap){0}; }
