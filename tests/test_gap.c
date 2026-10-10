#include "gap.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(void) {
    Gap g = {0}; char reference[100000] = {0}; size_t len = 0;
    srand(42);
    for (int i = 0; i < 20000; i++) {
        size_t at = (size_t)rand() % (len + 1);
        if (rand() % 2 && len) {
            size_t n = (size_t)rand() % (len - at + 1);
            assert(gap_delete(&g, at, n)); memmove(reference+at, reference+at+n, len-at-n); len -= n;
        } else {
            char s[16]; size_t n = (size_t)rand()%sizeof s;
            memset(s, 'a'+rand()%26, n);
            assert(gap_insert(&g, at, s, n)); memmove(reference+at+n, reference+at, len-at); memcpy(reference+at, s, n); len += n;
        }
        char *s = gap_text(&g); assert(s); assert(gap_length(&g) == len); assert(!memcmp(s, reference, len)); assert(s[len] == 0); free(s);
    }
    assert(!gap_insert(&g, len+1, "x", 1)); assert(!gap_delete(&g, 0, len+1));
    gap_free(&g); assert(gap_insert(&g, 0, "é🙂", 6)); assert(gap_insert(&g, 2, "中", 3));
    char *s = gap_text(&g); assert(!strcmp(s, "é中🙂")); free(s); gap_free(&g);
    puts("gap: 20,000 randomized edits and UTF-8 checks passed");
}
