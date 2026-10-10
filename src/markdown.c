#include "markdown.h"
#include <string.h>
static void tag(GtkTextBuffer *b, const char *name, int line, int from, int to) {
    GtkTextIter a, z;
    gtk_text_buffer_get_iter_at_line_index(b, &a, line, from);
    gtk_text_buffer_get_iter_at_line_index(b, &z, line, to);
    gtk_text_buffer_apply_tag_by_name(b, name, &a, &z);
}
void markdown_init(GtkTextBuffer *b) {
    gtk_text_buffer_create_tag(b, "syntax", "invisible", TRUE, NULL);
    for (int i = 1; i <= 6; i++) {
        char name[8]; g_snprintf(name, sizeof name, "h%d", i);
        gtk_text_buffer_create_tag(b, name, "scale", 2.1 - i * .16, "weight", PANGO_WEIGHT_BOLD,
                                  "pixels-above-lines", 14, "pixels-below-lines", 7, NULL);
    }
    gtk_text_buffer_create_tag(b, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    gtk_text_buffer_create_tag(b, "italic", "style", PANGO_STYLE_ITALIC, NULL);
    gtk_text_buffer_create_tag(b, "code", "family", "monospace", "paragraph-background", "#22252d", "foreground", "#d8dee9", NULL);
    gtk_text_buffer_create_tag(b, "inline", "family", "monospace", "background", "#22252d", "foreground", "#d8dee9", NULL);
    gtk_text_buffer_create_tag(b, "muted", "foreground", "#8793a5", NULL);
    gtk_text_buffer_create_tag(b, "keyword", "foreground", "#c792ea", "weight", PANGO_WEIGHT_BOLD, NULL);
    gtk_text_buffer_create_tag(b, "string", "foreground", "#c3e88d", NULL);
    gtk_text_buffer_create_tag(b, "number", "foreground", "#f78c6c", NULL);
    gtk_text_buffer_create_tag(b, "link", "foreground", "#82aaff", "underline", PANGO_UNDERLINE_SINGLE, NULL);
}
static gboolean keyword(const char *s, int n) {
    static const char *words[] = {"if","else","for","while","return","void","int","char","float","double","unsigned","struct","typedef","const","static","sizeof","switch","case","break","bool","include","define","def","class","import","from","as","fn","let","mut","pub","use","true","false","null","None","True","False","function","var","async","await",NULL};
    for (int i = 0; words[i]; i++) if ((int)strlen(words[i]) == n && !memcmp(words[i], s, n)) return TRUE;
    return FALSE;
}
static void code(GtkTextBuffer *b, const char *s, int line) {
    int n = (int)strlen(s);
    tag(b, "code", line, 0, n);
    for (int i = 0; i < n;) {
        int start = i;
        if ((s[i] == '/' && (s[i+1] == '/' || s[i+1] == '*')) || s[i] == '#') { tag(b, "muted", line, i, n); break; }
        if (s[i] == '"' || s[i] == '\'') {
            char quote = s[i++];
            while (i < n) { if (s[i] == '\\' && i+1 < n) i += 2; else if (s[i++] == quote) break; }
            tag(b, "string", line, start, i);
        } else if (g_ascii_isdigit(s[i])) {
            while (i < n && (g_ascii_isalnum(s[i]) || s[i] == '.')) i++;
            tag(b, "number", line, start, i);
        } else if (g_ascii_isalpha(s[i]) || s[i] == '_') {
            while (i < n && (g_ascii_isalnum(s[i]) || s[i] == '_')) i++;
            if (keyword(s+start, i-start)) tag(b, "keyword", line, start, i);
        } else i = (int)(g_utf8_next_char(s+i) - s);
    }
}
void markdown_style(GtkTextBuffer *b) {
    GtkTextIter a, z; gtk_text_buffer_get_bounds(b, &a, &z);
    gtk_text_buffer_remove_all_tags(b, &a, &z);
    char *text = gtk_text_buffer_get_text(b, &a, &z, TRUE);
    char **lines = g_strsplit(text, "\n", -1);
    char fence = 0; int fence_len = 0;
    for (int line = 0; lines[line]; line++) {
        char *s = lines[line]; int n = (int)strlen(s), indent = 0;
        while (indent < 3 && s[indent] == ' ') indent++;
        int run = 0; char c = s[indent];
        if (c == '`' || c == '~') while (s[indent+run] == c) run++;
        gboolean closing = fence && c == fence && run >= fence_len;
        if (closing) { for (int j = indent+run; s[j]; j++) if (!g_ascii_isspace(s[j])) closing = FALSE; }
        if ((!fence && run >= 3) || closing) {
            tag(b, "syntax", line, 0, n);
            if (fence) fence = 0; else { fence = c; fence_len = run; }
            continue;
        }
        if (fence) { code(b, s, line); continue; }
        int h = 0; while (s[indent+h] == '#' && h < 7) h++;
        if (h > 0 && h <= 6 && (s[indent+h] == ' ' || !s[indent+h])) {
            char name[8]; g_snprintf(name, sizeof name, "h%d", h); tag(b, name, line, 0, n);
            tag(b, "syntax", line, 0, indent+h+(s[indent+h] == ' '));
        }
        if (s[indent] == '>') {
            tag(b, "muted", line, 0, n);
            tag(b, "syntax", line, 0, indent+1+(s[indent+1] == ' '));
        }
        for (int i = 0; i < n;) {
            if (s[i] == '\\' && i+1 < n) { i = (int)(g_utf8_next_char(s+i+1)-s); continue; }
            const char *style = NULL; int width = 1;
            if (s[i] == '`') style = "inline";
            else if (s[i] == '*' || s[i] == '_') { width = s[i+1] == s[i] ? 2 : 1; style = width == 2 ? "bold" : "italic"; }
            if (style) {
                char delimiter[3] = {s[i], width == 2 ? s[i] : 0, 0};
                char *end = strstr(s+i+width, delimiter);
                if (end && end > s+i+width) { int finish = (int)(end-s)+width; tag(b, style, line, i, finish);
                    tag(b, "syntax", line, i, i+width);
                    tag(b, "syntax", line, (int)(end-s), finish);
                    i = finish; continue; }
            }
            gboolean image = s[i] == '!' && s[i+1] == '[';
            if (s[i] == '[' || image) {
                char *end = strstr(s+i+(image ? 1 : 0), "]("); char *close = end ? strchr(end+2, ')') : NULL;
                if (close) { tag(b, "link", line, i, (int)(close-s)+1);
                    tag(b, "syntax", line, i, i+1+(image ? 1 : 0));
                    tag(b, "syntax", line, (int)(end-s), (int)(close-s)+1);
                    i = (int)(close-s)+1; continue; }
            }
            i = (int)(g_utf8_next_char(s+i)-s);
        }
    }
    g_strfreev(lines); g_free(text);
}

void markdown_source(GtkTextBuffer *b, gboolean show) {
    GtkTextTag *syntax = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(b), "syntax");
    g_object_set(syntax, "invisible", !show, NULL);
}
