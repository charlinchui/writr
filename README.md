# Writr

A minimal, native Markdown editor written in C for Arch Linux and Omarchy.
One window, one document, an uncluttered writing surface with no toolbar, footer,
or title bar. An optional workspace tree lives behind Ctrl+T. Uses GTK 3 on Wayland
or X11 and follows your GTK theme. No browser, Electron, or C++ dependency.

## Build and run

```sh
sudo pacman -S --needed base-devel gtk3 libpng pkgconf ttf-opensans
make
./writr
./writr notes.md
./writr /path/to/notes-workspace
```

Install the binary and desktop launcher with `sudo make install`.
Use `make DESTDIR=/tmp/writr-install install` for a staged installation.

## Editing

The editor uses Open Sans, with a system sans-serif fallback if it is unavailable.
Empty notes show a larger, faint “Start typing…” prompt at the cursor and basic shortcut
hints centered in the writing area. These disappear when you type and are never
saved in the document.

Keyboard shortcuts open, save, create, and close documents. Creation uses a small
inline input: Enter confirms and Escape cancels. Closing a document
returns to a blank writing surface. Unsaved changes prompt you to save, discard,
or cancel. Canceling a file picker preserves the current document.

| Shortcut | Action |
| --- | --- |
| Ctrl+T | Toggle workspace tree |
| Ctrl+Shift+O | Create, open, or switch workspace folder |
| Ctrl+Shift+N | Create a note in the selected folder |
| Ctrl+Alt+N | Create a folder in the selected folder |
| Ctrl+Shift+R | Refresh workspace tree |
| Ctrl+Shift+M | Show/hide Markdown source markers |
| Ctrl+N | Create a note in the selected folder |
| Ctrl+PageUp / Ctrl+PageDown | Previous / next note |
| Ctrl+O | Open |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save as |
| Ctrl+W | Close document |
| Ctrl+Q | Quit |
| Ctrl+Z | Undo |
| Ctrl+Shift+Z / Ctrl+Y | Redo |
| Ctrl+A / C / X / V | Select all / copy / cut / paste |
| Ctrl+Shift+V | Paste plain text |

Headings (`#` through `######`) have larger, bold type. Bold, italic, inline
code, links, and quotes receive inline styling. Backtick and tilde fences have
monospace code with keyword, number, string, and comment highlighting. Markdown
markers are hidden in the writing view: `# Title` displays as **Title**,
`**bold**` as **bold**, and links display their label. Code fences and their language
names are hidden; code content stays visible and highlighted. Toggle Ctrl+Shift+M
to show source when editing delimiters or link destinations, or copying raw
Markdown. Formatting never removes bytes from the document or saved file.
Unicode input, selection, clipboard, and input methods are handled by GTK.

Empty headings, quotes, inline formatting, and code fences keep their markers
visible until they contain text. A fence's language label alone does not count
as code content.

## Pasting images and links

Ctrl+V (or Shift+Insert) accepts clipboard images and copied local image files.
Images are saved as PNGs in an `assets/` directory beside the current note,
with unique filenames, then inserted as `![image](assets/pasted-….png)`.
An untitled document uses the selected workspace folder. Alpha transparency is
preserved. The writing view shows the actual image, scaled to fit the text area,
and reserves space for it in the document. Click the image to select it; Delete
removes it, Ctrl+C copies its image data, and Ctrl+X cuts it.
Up/Down skip image-only blocks, including consecutive images. Click beneath an
image or move past it to continue writing. Backspace immediately after a block
and Delete immediately before it remove the image; source mode allows editing
individual characters of its Markdown. Undo/redo restores
the Markdown reference and its preview. Ctrl+Shift+M switches to the editable
Markdown source and hides the preview. Images already referenced in a note
also render when the note is reopened. Local and `file://` image references are
supported; remote images and missing files retain their alt label.

A single pasted HTTP/HTTPS URL becomes `[https://example.com](https://example.com)`.
If text is selected, that text becomes the link label. URLs pasted inside fenced
or inline code remain plain text, as does ordinary prose or existing Markdown.
Ctrl+Shift+V bypasses conversion and pastes plain text. Paste forms one undo step;
image assets are retained on undo so redo continues to work. Clipboard results
arriving after switching notes are ignored.

Image references are relative to the note's directory. Keep its `assets/` folder
with the note when moving or exporting it; Save As to another directory does not
copy image assets automatically.

Previews are drawn over space reserved in the text layout. No pixbuf objects or
placeholder characters are inserted into either text buffer, so offsets, undo
records, and saved Markdown continue to refer to the original source bytes.

## Workspaces

A workspace is an ordinary folder containing Markdown/text notes and subfolders;
no project file is needed. Ctrl+T toggles a resizable tree, hidden by default.
On first launch, a single inline input asks you to create or open a workspace;
the suggested path is `~/Documents/Notes`. Enter creates missing directories or
opens an existing folder; Escape closes the app if no workspace is available.
The last workspace is remembered in `$XDG_CONFIG_HOME/writr/workspace` (normally
`~/.config/writr/workspace`) whenever you open or switch workspaces. On the next
launch it opens automatically, without the setup input; setup is shown only
when there is no valid remembered folder. Ctrl+Shift+O opens the same
input to switch folders without closing the current document. A workspace with
no visible notes, including in its subfolders, receives `undefined.md`; an
existing note is never replaced. The first note opens automatically. Passing a directory on the command line opens its
tree immediately. Enter or double-click opens a note or expands a folder;
switching notes prompts about unsaved changes. Ctrl+PageUp and Ctrl+PageDown
move through notes in sorted tree order, including collapsed folders, wrapping
at either end. The tree selection follows the active note.

Select a folder, then use Ctrl+Shift+N for a new note or Ctrl+Alt+N for a new
subfolder. Both use just an inline input, with Enter to create and Escape to
cancel. Entering no note name creates `Untitled.md` (or `Untitled 2.md`, etc. if
needed). With a file selected, creation targets its parent folder; without a
selection, it targets the workspace root. Note names default to `.md`. Existing
files are never overwritten by creation. Save As starts in the current file's
folder, or the selected workspace folder for an untitled document.

Folders load on expansion. The tree shows `.md`, `.markdown`, and `.txt` files,
with folders first; hidden entries and symlinks are omitted. It refreshes after
saving or creating items, and when shown again. Ctrl+Shift+R refreshes external
changes while retaining expanded folders and selection where possible. The
workspace selection is remembered for the next launch.

Fresh `Untitled.md`, automatically numbered untitled notes, and the generated
`undefined.md` follow the first `# H1` heading as you type: `# Travel notes`
renames the file to `Travel notes.md`. Characters unsafe in filenames are
sanitized, and headings inside code fences are ignored. The filename follows
heading edits until the first explicit Save or Save As, then stays fixed.
Existing files opened from disk and notes given an explicit custom name are
never automatically renamed. Collisions leave both files intact; Save reports
the collision so you can change the heading or use Save As. Empty placeholder
creation does not count as the first save. Draft eligibility lasts for the
current app session; existing files on a subsequent launch are treated as saved.
Renaming the placeholder does not save the document contents: use Ctrl+S.

## Design and limits

`src/gap.c` owns a growable byte gap buffer. Insert/delete signals update it
before GTK performs the corresponding display edit. Saves read the gap buffer,
not the widget. GTK maintains its own text layout representation; this is a
native text view with a gap-backed document, rather than a custom text renderer.
Undo/redo stores edit deltas grouped by GTK user actions. Dirty tracking compares
the document against the last saved text, including after undo.

GIO replaces files rather than truncating them in place. An opened file's ETag
is checked during subsequent saves; detected external changes produce an error
and preserve your edits. Save As can be used to save a separate copy. Detection
depends on the filesystem's ETag implementation. Native file choosers can use the
desktop portal with `GTK_USE_PORTAL=1` and a configured portal backend.

This initial version styles common Markdown constructs; it is not a complete
CommonMark renderer or a separate preview pane. Local images render inside
the editor; tables remain text. Recognized syntax is hidden using display tags, while raw source remains in
both text buffers. For precise edits at invisible boundaries, use the source
toggle. Code
highlighting is a lightweight shared lexer, not a language-specific parser;
multiline strings/comments and nested Markdown are not fully parsed. Documents
must be UTF-8 without NUL bytes. Styling rescans the document after an 80 ms
pause, and large files are not a design target. There is no crash recovery or
autosave yet.

## Verification

```sh
make test check-ui
```

The gap test checks 20,000 randomized insertions/deletions against a reference
string plus UTF-8 bytes and invalid ranges. The editor test runs without a display
and checks gap/widget consistency, grouped undo/redo, Markdown tags, file round
trips, dirty state, external-change conflicts, hidden syntax, visible text, and
source toggling. Workspace tests check lazy folder loading, sorting, filtering,
safe creation, failed folder switches, initial-note seeding, and navigation
across subfolders. Editor tests also cover first-save filename locking, existing
untitled files, title sanitization, rename collisions, and reopening the
remembered workspace without a setup prompt. Paste tests verify URL escaping,
code contexts, PNG pixels and alpha transparency, asset failures, grouped undo,
and stale clipboard callbacks. Image tests check local path resolution, source
offsets, exclusion of code examples, and decoded PNG pixels. With a desktop
display available, they also check rendered pixels and source-mode switching;
those visual checks are explicitly skipped in a headless environment. Native windows, input methods,
and portal dialogs should also be checked in an actual desktop session.

## Arch package

Create a source archive and build the local package:

```sh
mkdir -p /tmp/writr-package/writr-0.1.0
cp -r src tests packaging Makefile README.md LICENSE /tmp/writr-package/writr-0.1.0/
tar -C /tmp/writr-package -czf packaging/writr-0.1.0.tar.gz writr-0.1.0
cd packaging
makepkg -si
```

The supplied PKGBUILD is for a local source archive. A published release should
use a hosted source URL and pinned checksum instead of `SKIP`. No package has
been published to the AUR or Omarchy repositories.

Inspired by [Omawrite](https://github.com/omacom/omawrite)'s minimal writing
interface. This implementation uses its own C code. MIT licensed.
