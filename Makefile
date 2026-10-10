CC ?= cc
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr
CFLAGS ?= -O2 -g
WARN = -std=c11 -Wall -Wextra -Wpedantic
GTK_CFLAGS = $(shell $(PKG_CONFIG) --cflags gtk+-3.0 libpng)
GTK_LIBS = $(shell $(PKG_CONFIG) --libs gtk+-3.0 libpng)

all: writr
writr: src/main.c src/gap.c src/markdown.c src/workspace.c src/paste.c src/images.c src/gap.h src/markdown.h src/workspace.h src/paste.h src/images.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(GTK_CFLAGS) src/main.c src/gap.c src/markdown.c src/workspace.c src/paste.c src/images.c $(LDFLAGS) $(GTK_LIBS) -o $@
tests/test_gap: tests/test_gap.c src/gap.c src/gap.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Isrc tests/test_gap.c src/gap.c $(LDFLAGS) -o $@
tests/test_editor: tests/test_editor.c src/main.c src/gap.c src/markdown.c src/workspace.c src/paste.c src/images.c src/gap.h src/markdown.h src/workspace.h src/paste.h src/images.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(GTK_CFLAGS) -Isrc tests/test_editor.c src/gap.c src/markdown.c src/workspace.c src/paste.c src/images.c $(LDFLAGS) $(GTK_LIBS) -o $@
tests/test_workspace: tests/test_workspace.c src/workspace.c src/workspace.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(GTK_CFLAGS) -Isrc tests/test_workspace.c src/workspace.c $(LDFLAGS) $(GTK_LIBS) -o $@
tests/test_paste: tests/test_paste.c src/paste.c src/paste.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(GTK_CFLAGS) -Isrc tests/test_paste.c src/paste.c $(LDFLAGS) $(GTK_LIBS) -o $@
tests/test_images: tests/test_images.c src/images.c src/images.h src/paste.c src/paste.h src/markdown.c src/markdown.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(GTK_CFLAGS) -Isrc tests/test_images.c src/images.c src/paste.c src/markdown.c $(LDFLAGS) $(GTK_LIBS) -o $@
test: tests/test_gap tests/test_workspace tests/test_paste tests/test_images
	./tests/test_gap
	./tests/test_workspace
	./tests/test_paste
	./tests/test_images
check-ui: tests/test_editor
	./tests/test_editor
install: writr
	install -Dm755 writr $(DESTDIR)$(PREFIX)/bin/writr
	install -Dm644 packaging/writr.desktop $(DESTDIR)$(PREFIX)/share/applications/writr.desktop
clean:
	rm -f writr tests/test_gap tests/test_editor tests/test_workspace tests/test_paste tests/test_images
.PHONY: all test check-ui install clean
