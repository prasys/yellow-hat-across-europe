/*
 * launcher.c - the Linux launcher: choose the disc, set how the game looks
 * and starts, see the saved games, press Play. The same job as the macOS
 * (the .swift files in ..) and Windows (../win/launcher.c) launchers, on plain GTK 4.
 * File choosers go through the desktop portal, so on KDE they are KDE's.
 *
 * All drawing here is the launcher's own (a yellow hat made of a few shapes);
 * nothing is taken from the game.
 *
 * It edits madeline.ini, the "key=value" file the engine (src/main.c) reads
 * and writes, changing only the keys it knows and leaving every other line as
 * it found it.
 *
 *   yellow-hat-across-europe [--snapshot FILE.png] [disc image or folder]
 *
 * --snapshot writes a picture of the window once it has settled and quits
 * (for checking the layout). MADELINE_SAVE_DIR moves the settings and saves,
 * as it does for the engine: tests use it so they never touch a player's own.
 * MADELINE_ENGINE names the engine (default: `madeline` beside this program).
 *
 * A release (the AppImage, scripts/package/linux.sh) carries no engine: the
 * engine is the game's code and is built on the player's machine by the kit
 * (kit/madeline_kit.py), into the data folder. With a kit (MADELINE_KIT, else
 * ../lib/yellow-hat-across-europe/kit), unpacking, checking and building go
 * through it; without one (a build from a checkout) they go through make.
 * MADELINE_ENGINE_LIBS (set by the AppImage) is the engine's LD_LIBRARY_PATH,
 * given to the engine alone.
 */
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "soundfonts.h"

/* The desktop entry (`make desktop`) and the icon have this name, and the
 * engine's window is given it too, so a Wayland desktop puts both windows
 * under the one entry and its hat. */
#define APP_ID "io.github.prasys.YellowHatAcrossEurope"
#define TITLE "The Little Girl in the Yellow Hat Across Europe"
#define KEYS_HINT "F8 xBR  ·  F9 screenshot  ·  F10 smoothing  ·  F11 full screen  ·  F12 scaling"

static const char *const SCALE_KEYS[] = { "smooth", "sharp", "crt", "nearest", "integer", NULL };
static const char *const SCALE_LABELS[] = { "Smooth", "Sharp", "CRT monitor", "Blocky pixels", "Whole multiples only", NULL };
static const char *const BARS_LABELS[] = { "Soft blur", "Black", NULL };

static GtkWindow *g_win;
static GtkWidget *g_disc_status, *g_disc_path, *g_engine_status, *g_engine_hint, *g_choose, *g_build,
                 *g_skip, *g_name, *g_full, *g_smooth, *g_xbr, *g_scale, *g_bars, *g_saves,
                 *g_sf_status, *g_sf_choose, *g_sf_reset, *g_status, *g_play;

static char g_data[PATH_MAX];          /* settings, saves, screenshots: the engine's folder (src/platform.c) */
static char g_ini[PATH_MAX + 16];
static char g_engine[PATH_MAX + 32];   /* the engine: beside this program, or a release's in the data folder */
static char g_root[PATH_MAX];          /* the checkout this was built from, for building an engine */
static char g_kit[PATH_MAX + 64];      /* a release's kit, or empty */
static char g_disc[PATH_MAX];
static char g_soundfont[PATH_MAX];     /* soundfont= in madeline.ini; empty: the system's */
static char g_footer[PATH_MAX + 256]; /* what the footer says; empty: the key hints */
static char g_snapshot[PATH_MAX];
static int g_busy, g_building, g_playing, g_then_build, g_loading;
static int g_engine_ok = -1;           /* was the engine recompiled from this disc's MADEURO.EXE? -1: not known yet */

static int file_exists(const char *p) { return g_file_test(p, G_FILE_TEST_IS_REGULAR); }
static int readable(const char *p) { return p && *p && access(p, R_OK) == 0; }
static int disc_valid(void) {
    char exe[PATH_MAX + 16];
    if (!g_disc[0]) return 0;
    snprintf(exe, sizeof exe, "%s/MADEURO.EXE", g_disc);
    return file_exists(exe);
}
static void say(const char *text) { g_strlcpy(g_footer, text, sizeof g_footer); }

/* ---- madeline.ini ------------------------------------------------------- */

#define INI_LINES 256
static char g_lines[INI_LINES][1024];
static int g_nlines;

static void ini_load(void) {
    FILE *f = fopen(g_ini, "r");
    g_nlines = 0;
    if (!f) return;
    while (g_nlines < INI_LINES && fgets(g_lines[g_nlines], sizeof g_lines[0], f)) {
        g_lines[g_nlines][strcspn(g_lines[g_nlines], "\r\n")] = 0;
        g_nlines++;
    }
    fclose(f);
}
/* The value of a "key = value  # comment" line, or NULL if the line is not that key's. */
static const char *ini_line_value(const char *line, const char *key, char *out, size_t n) {
    size_t k = strlen(key);
    const char *p = line + strspn(line, " \t");
    if (strncmp(p, key, k)) return NULL;
    p += k; p += strspn(p, " \t");
    if (*p != '=') return NULL;
    p++; p += strspn(p, " \t");
    snprintf(out, n, "%s", p);
    out[strcspn(out, "#")] = 0;
    for (size_t len = strlen(out); len && (out[len - 1] == ' ' || out[len - 1] == '\t'); ) out[--len] = 0;
    return out;
}
static int ini_get(const char *key, char *out, size_t n) {       /* the last one wins, as in the engine */
    int found = 0;
    char v[1024];
    for (int i = 0; i < g_nlines; i++)
        if (ini_line_value(g_lines[i], key, v, sizeof v)) { snprintf(out, n, "%s", v); found = 1; }
    return found;
}
static int ini_flag(const char *key, int fallback) {
    char v[1024];
    if (!ini_get(key, v, sizeof v)) return fallback;
    return atoi(v) != 0 || !g_ascii_strcasecmp(v, "yes") || !g_ascii_strcasecmp(v, "true") || !g_ascii_strcasecmp(v, "on");
}
static void ini_set(const char *key, const char *value) {
    char v[1024];
    int at = -1, n = 0;
    for (int i = 0; i < g_nlines; i++) {
        if (ini_line_value(g_lines[i], key, v, sizeof v)) {
            if (at >= 0) continue;                              /* a second line for the key: drop it */
            at = n;
        }
        if (n != i) memcpy(g_lines[n], g_lines[i], sizeof g_lines[0]);
        n++;
    }
    g_nlines = n;
    if (at < 0 && g_nlines < INI_LINES) at = g_nlines++;
    if (at >= 0) snprintf(g_lines[at], sizeof g_lines[0], "%s=%s", key, value);
}
static int ini_write(void) {
    char tmp[PATH_MAX + 32];
    FILE *f;
    g_mkdir_with_parents(g_data, 0755);
    snprintf(tmp, sizeof tmp, "%s.new", g_ini);
    if (!(f = fopen(tmp, "w"))) return 0;
    for (int i = 0; i < g_nlines; i++) fprintf(f, "%s\n", g_lines[i]);
    if (fclose(f) != 0) return 0;
    return rename(tmp, g_ini) == 0;
}

static int checked(GtkWidget *w) { return gtk_check_button_get_active(GTK_CHECK_BUTTON(w)); }
static void check(GtkWidget *w, int on) { gtk_check_button_set_active(GTK_CHECK_BUTTON(w), on); }
static int selected(GtkWidget *w) { guint s = gtk_drop_down_get_selected(GTK_DROP_DOWN(w)); return s == GTK_INVALID_LIST_POSITION ? 0 : (int)s; }

/* The window's settings into the file. */
static void save_settings(void) {
    if (g_loading) return;                              /* the file is being put into the window, not the other way */
    ini_load();
    ini_set("cd", g_disc);
    ini_set("skip_intro", checked(g_skip) ? "1" : "0");
    ini_set("fullscreen", checked(g_full) ? "1" : "0");
    ini_set("remember_name", checked(g_name) ? "1" : "0");
    ini_set("smooth", checked(g_smooth) ? "1" : "0");
    ini_set("upscale", checked(g_xbr) ? "xbr" : "off");
    ini_set("scale", SCALE_KEYS[selected(g_scale)]);
    ini_set("bars", selected(g_bars) == 1 ? "black" : "blur");
    ini_set("soundfont", g_soundfont);                  /* empty: the engine looks for the system's */
    if (!ini_write()) say("Could not save the settings.");
}
/* The file into the window; also after a game, which may have changed it (F8, F10, F11, F12). */
static void load_settings(int first) {
    char v[1024];
    g_loading = 1;
    ini_load();
    if (first) {
        if (ini_get("cd", v, sizeof v)) g_strlcpy(g_disc, v, sizeof g_disc);
        check(g_skip, ini_flag("skip_intro", 0));
        check(g_name, ini_flag("remember_name", 1));
    }
    g_soundfont[0] = 0;
    if (ini_get("soundfont", v, sizeof v)) g_strlcpy(g_soundfont, v, sizeof g_soundfont);
    check(g_full, ini_flag("fullscreen", 0));
    check(g_smooth, ini_flag("smooth", 1));
    check(g_xbr, ini_get("upscale", v, sizeof v) && !strcmp(v, "xbr"));
    guint s = 0;
    if (ini_get("scale", v, sizeof v))
        for (guint i = 0; SCALE_KEYS[i]; i++) if (!strcmp(v, SCALE_KEYS[i])) s = i;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_scale), s);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_bars), ini_get("bars", v, sizeof v) && !strcmp(v, "black") ? 1 : 0);
    g_loading = 0;
}

/* ---- the saved games ---------------------------------------------------- */

typedef struct { char name[64]; gint64 when; } Save;
static int newer_first(const void *a, const void *b) {
    gint64 x = ((const Save *)a)->when, y = ((const Save *)b)->when;
    return x < y ? 1 : x > y ? -1 : 0;
}
static GtkWidget *label_with(const char *text, const char *css, float xalign);

/* A save begins with the player's name (NUL-terminated, 16 bytes, Windows
 * Latin-1); a non-zero word at 0xF0 marks a file the game discards (docs/NOTES.md). */
static void load_saves(void) {
    Save saves[10];
    int n = 0;
    for (int slot = 0; slot < 10; slot++) {
        char path[PATH_MAX + 40];
        unsigned char head[0xF2];
        GStatBuf st;
        FILE *f;
        snprintf(path, sizeof path, "%s/CWONDERS/MADEURO/%d.sav", g_data, slot);
        if (!(f = fopen(path, "rb"))) continue;
        if (fread(head, 1, sizeof head, f) == sizeof head && !head[0xF0] && !head[0xF1] && head[0] && g_stat(path, &st) == 0) {
            char raw[16];
            memcpy(raw, head, 15); raw[15] = 0;
            char *utf8 = g_convert(raw, -1, "UTF-8", "CP1252", NULL, NULL, NULL);
            g_strlcpy(saves[n].name, utf8 ? utf8 : raw, sizeof saves[n].name);
            g_free(utf8);
            saves[n].when = st.st_mtime;
            n++;
        }
        fclose(f);
    }
    qsort(saves, n, sizeof saves[0], newer_first);
    gtk_list_box_remove_all(GTK_LIST_BOX(g_saves));
    for (int i = 0; i < n; i++) {
        GDateTime *dt = g_date_time_new_from_unix_local(saves[i].when);
        char *when = g_date_time_format(dt, "%A %e %B %Y, %H:%M");
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        GtkWidget *name = label_with(saves[i].name, "save-name", 0);
        gtk_widget_set_hexpand(name, TRUE);
        gtk_box_append(GTK_BOX(row), name);
        gtk_box_append(GTK_BOX(row), label_with(when, "hint", 1));
        gtk_list_box_append(GTK_LIST_BOX(g_saves), row);
        g_free(when);
        g_date_time_unref(dt);
    }
    if (!n) gtk_list_box_append(GTK_LIST_BOX(g_saves), label_with("None yet. In the game, MENU then QUIT saves.", "hint", 0));
}

/* ---- what the window shows ---------------------------------------------- */

/* Which SoundFont the engine will play the music with (src/midi.c), or "" for none. */
static const char *soundfont_in_use(void) {
    if (g_soundfont[0]) return readable(g_soundfont) ? g_soundfont : "";
    for (size_t i = 0; i < MADELINE_SOUNDFONT_COUNT; i++)
        if (readable(madeline_soundfont_paths[i])) return madeline_soundfont_paths[i];
    return "";
}

static void tone(GtkWidget *w, const char *css) {      /* one of ok / warn / NULL */
    gtk_widget_remove_css_class(w, "ok");
    gtk_widget_remove_css_class(w, "warn");
    if (css) gtk_widget_add_css_class(w, css);
}
static int can_build(void) { return (g_root[0] || g_kit[0]) && disc_valid() && !g_busy && !g_building && !g_playing; }
static int can_play(void) { return disc_valid() && g_engine_ok == 1 && !g_busy && !g_building && !g_playing && file_exists(g_engine); }

static void update(void) {
    int valid = disc_valid();
    gtk_label_set_text(GTK_LABEL(g_disc_status), g_busy ? "Unpacking the disc image…" : valid ? "✓  Disc found" : "No disc chosen yet");
    tone(g_disc_status, valid && !g_busy ? "ok" : NULL);
    const char *home = g_get_home_dir();                /* a path in the home folder, shown as ~/... */
    size_t hl = strlen(home);
    char shown[PATH_MAX + 2];
    if (hl > 1 && !strncmp(g_disc, home, hl) && g_disc[hl] == '/') snprintf(shown, sizeof shown, "~%s", g_disc + hl);
    else g_strlcpy(shown, g_disc, sizeof shown);
    gtk_label_set_text(GTK_LABEL(g_disc_path), g_disc[0] ? shown : g_kit[0] ? "Choose your own disc image (.iso, or the .zip it came in) or a folder with the disc's files."
                                                                           : "Choose your own disc image (.iso) or a folder with the disc's files.");
    gtk_label_set_text(GTK_LABEL(g_engine_status), g_building ? "Recompiling the game from your disc…"
                       : g_engine_ok == 1 ? "✓  Engine built for this disc"
                       : g_engine_ok == 0 ? "This disc needs its engine built" : "Checking the engine…");
    tone(g_engine_status, g_building ? NULL : g_engine_ok == 1 ? "ok" : g_engine_ok == 0 ? "warn" : NULL);
    gtk_widget_set_visible(g_engine_status, valid);
    gtk_widget_set_visible(g_engine_hint, valid);
    gtk_widget_set_visible(g_build, valid && g_engine_ok == 0 && !g_building);
    gtk_widget_set_sensitive(g_build, can_build());
    gtk_widget_set_sensitive(g_choose, !g_busy && !g_building && !g_playing);
    gtk_widget_set_sensitive(g_play, can_play());
    gtk_button_set_label(GTK_BUTTON(g_play), g_playing ? "Playing" : "▶  Play");

    const char *sf = soundfont_in_use();
    char text[PATH_MAX + 64];
    if (g_soundfont[0] && !sf[0]) snprintf(text, sizeof text, "%s cannot be read: the music will be silent.", g_soundfont);
    else if (g_soundfont[0]) snprintf(text, sizeof text, "✓  %s", sf);
    else if (sf[0]) snprintf(text, sizeof text, "✓  The system's: %s", sf);
    else snprintf(text, sizeof text, "No SoundFont found: the music will be silent. Install soundfont-fluid (Arch) or fluid-soundfont-gm (Debian, Ubuntu), or choose a .sf2 file.");
    gtk_label_set_text(GTK_LABEL(g_sf_status), text);
    tone(g_sf_status, sf[0] ? "ok" : "warn");
    gtk_widget_set_visible(g_sf_reset, g_soundfont[0] != 0);

    gtk_label_set_text(GTK_LABEL(g_status), g_footer[0] ? g_footer : KEYS_HINT);
}

/* ---- other programs: the engine, the build, the unpacking ---------------- */

typedef void (*done_fn)(int code);
static void job_done(GObject *src, GAsyncResult *res, gpointer fn) {
    GSubprocess *p = G_SUBPROCESS(src);
    int code = -1;
    if (g_subprocess_wait_finish(p, res, NULL))
        code = g_subprocess_get_if_exited(p) ? g_subprocess_get_exit_status(p) : 128 + g_subprocess_get_term_sig(p);
    ((done_fn)fn)(code);
    g_object_unref(p);
}
/* Run a program, its output into `log` (or nowhere), and call `fn` with its exit code (-1: it would not start).
 * The engine (argv[0] == g_engine) is given the AppImage's libraries, which no other program must see. */
static void start_job(const char *const *argv, const char *cwd, const char *log, done_fn fn) {
    GSubprocessLauncher *l = g_subprocess_launcher_new(log ? G_SUBPROCESS_FLAGS_STDERR_MERGE
                                                           : G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    GError *err = NULL;
    if (log) g_subprocess_launcher_set_stdout_file_path(l, log);
    if (cwd) g_subprocess_launcher_set_cwd(l, cwd);
    /* The engine's window under this program's name (SDL2, SDL3 and sdl2-compat each read one of these). */
    g_subprocess_launcher_setenv(l, "SDL_APP_ID", APP_ID, TRUE);
    g_subprocess_launcher_setenv(l, "SDL_VIDEO_WAYLAND_WMCLASS", APP_ID, TRUE);
    g_subprocess_launcher_setenv(l, "SDL_VIDEO_X11_WMCLASS", APP_ID, TRUE);
    const char *libs = getenv("MADELINE_ENGINE_LIBS");
    if (libs && *libs && argv[0] == g_engine) g_subprocess_launcher_setenv(l, "LD_LIBRARY_PATH", libs, TRUE);
    GSubprocess *p = g_subprocess_launcher_spawnv(l, argv, &err);
    g_object_unref(l);
    if (!p) { g_clear_error(&err); fn(-1); return; }
    g_subprocess_wait_async(p, NULL, job_done, (gpointer)fn);
}

static void check_engine(int then_build);
static void use_folder(const char *path);

/* The kit's tool, run by the kit's own Python: { python, -IB, madeline_kit.py, cmd, a, b }. -B: the kit is
 * read-only (an AppImage), so no bytecode is written beside it. */
static char g_kit_python[PATH_MAX + 96], g_kit_tool[PATH_MAX + 96], g_engine_dir[PATH_MAX + 16];
static void kit_argv(const char *argv[7], const char *cmd, const char *a, const char *b) {
    argv[0] = g_kit_python, argv[1] = "-IB", argv[2] = g_kit_tool, argv[3] = cmd, argv[4] = a, argv[5] = b, argv[6] = NULL;
}
static void build_log(char *out, size_t n) {
    if (g_kit[0]) snprintf(out, n, "%s/engine-build.log", g_data);
    else snprintf(out, n, "%s/build/launcher-build.log", g_root);
}

static void built(int code) {
    char log[PATH_MAX + 64];
    g_building = 0;
    if (code == 0) { say("Engine built for this disc."); check_engine(0); return; }
    g_engine_ok = 0;
    build_log(log, sizeof log);
    snprintf(g_footer, sizeof g_footer, "The build failed. See %s", log);
    update();
}
/* The engine is the game's own code, lifted from one particular MADEURO.EXE:
 * a disc with a different build needs its own. `make engine-into-app` lifts
 * this disc's, compiles it with the host and puts it beside this program. */
static void build_engine(void) {
    char cd[PATH_MAX + 8], log[PATH_MAX + 64];
    build_log(log, sizeof log);
    if (g_kit[0]) {                    /* a release: the kit lifts and compiles into the data folder */
        const char *argv[7];
        kit_argv(argv, "build", g_disc, g_engine_dir);
        g_mkdir_with_parents(g_data, 0755);
        g_building = 1;
        say("Recompiling the game from your disc… (one to three minutes the first time)");
        update();
        start_job(argv, NULL, log, built);
        return;
    }
    if (!g_root[0]) {
        say("This disc needs its own engine, and this program does not know where its source is. Run: make app CD=<disc folder>");
        update();
        return;
    }
    snprintf(cd, sizeof cd, "CD=%s", g_disc);
    const char *argv[] = { "make", "-C", g_root, "engine-into-app", cd, NULL };
    g_building = 1;
    say("Recompiling the game from your disc… (about a minute)");
    update();
    start_job(argv, NULL, log, built);
}

static void checked_engine(int code) {
    g_engine_ok = code == 0;
    update();
    if (!g_engine_ok && g_then_build) build_engine();
    g_then_build = 0;
}
/* Ask the engine whether it was built for this disc (`madeline --check`: 0 yes, 3 no);
 * with a kit, the kit, which also knows whether this version of the app built it. */
static void check_engine(int then_build) {
    g_then_build = then_build;
    if (!disc_valid()) { g_engine_ok = -1; update(); return; }
    if (g_kit[0]) {
        const char *argv[7];
        kit_argv(argv, "check", g_disc, g_engine_dir);
        g_engine_ok = -1;
        update();
        start_job(argv, NULL, NULL, checked_engine);
        return;
    }
    if (!file_exists(g_engine)) {
        g_engine_ok = 0;
        update();
        if (then_build) build_engine();
        return;
    }
    const char *argv[] = { g_engine, "--check", g_disc, NULL };
    g_engine_ok = -1;
    update();
    start_job(argv, NULL, NULL, checked_engine);
}

static void played(int code) {
    g_playing = 0;
    load_settings(0);
    load_saves();
    if (code == 0) say("");
    else snprintf(g_footer, sizeof g_footer, "The game stopped unexpectedly (code %d). See %s/last-run.log", code, g_data);
    update();
}
static void play(void) {
    char log[PATH_MAX + 32];
    if (!can_play()) return;
    snprintf(log, sizeof log, "%s/last-run.log", g_data);   /* what the engine printed, for when something is wrong */
    const char *argv[] = { g_engine, g_disc, NULL };
    g_playing = 1;
    say("Playing…");
    update();
    start_job(argv, g_disc, log, played);
}

static void use_folder(const char *path) {
    char was[PATH_MAX];
    size_t n;
    g_strlcpy(was, g_disc, sizeof was);
    g_strlcpy(g_disc, path, sizeof g_disc);
    for (n = strlen(g_disc); n > 1 && g_disc[n - 1] == '/'; ) g_disc[--n] = 0;
    if (disc_valid()) {
        say("Disc folder set.");
        save_settings();
        check_engine(1);
    } else {
        g_strlcpy(g_disc, was, sizeof g_disc);
        say("That folder is not the game's disc: there is no MADEURO.EXE in it.");
        update();
    }
}

static void unpacked(int code) {
    char dest[PATH_MAX + 8];
    g_busy = 0;
    snprintf(dest, sizeof dest, "%s/disc", g_data);
    if (code != 0) {
        say(code == (g_kit[0] ? 65 : 3) ? "That image is not the game's disc: there is no MADEURO.EXE on it."
            : g_kit[0] ? "Could not unpack that disc image. Is it a .iso, or a .zip with one inside?"
                       : "Could not unpack that disc image. It needs 7-Zip (7z) or bsdtar installed.");
        update();
        return;
    }
    use_folder(dest);
    if (disc_valid() && !strcmp(g_disc, dest) && !g_building) say("Disc unpacked.");
    update();
}
/* Unpack the player's own disc image into the data folder. bsdtar first, 7z
 * when bsdtar left nothing: libarchive 3.8 reads this disc as empty without
 * an error (docs/NOTES.md). Exit 2: no tool, 3: not the game's disc. */
static void use_image(const char *iso) {
    char dest[PATH_MAX + 8];
    snprintf(dest, sizeof dest, "%s/disc", g_data);
    if (g_kit[0]) {                    /* the kit reads .iso (and the .zip it is downloaded in) itself */
        const char *argv[] = { g_kit_python, "-IB", g_kit_tool, "extract", "--replace", iso, dest, NULL };
        g_mkdir_with_parents(g_data, 0755);
        g_busy = 1;
        say("Unpacking the disc image…");
        update();
        start_job(argv, NULL, NULL, unpacked);
        return;
    }
    g_mkdir_with_parents(dest, 0755);
    const char *argv[] = { "sh", "-c",
        "command -v bsdtar >/dev/null || command -v 7z >/dev/null || exit 2; "
        "command -v bsdtar >/dev/null && bsdtar -xf \"$1\" -C \"$2\" 2>/dev/null; "
        "[ -f \"$2/MADEURO.EXE\" ] || { command -v 7z >/dev/null && 7z x -y -o\"$2\" \"$1\" >/dev/null 2>&1; }; "
        "chmod -R u+rwX \"$2\"; [ -f \"$2/MADEURO.EXE\" ] || exit 3",
        "sh", iso, dest, NULL };
    g_busy = 1;
    say("Unpacking the disc image…");
    update();
    start_job(argv, NULL, NULL, unpacked);
}

/* ---- choosing files ----------------------------------------------------- */

static GListModel *filters(const char *name, const char *const *patterns) {
    GListStore *list = g_list_store_new(GTK_TYPE_FILE_FILTER);
    GtkFileFilter *f = gtk_file_filter_new(), *all = gtk_file_filter_new();
    gtk_file_filter_set_name(f, name);
    for (int i = 0; patterns[i]; i++) gtk_file_filter_add_pattern(f, patterns[i]);
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    g_list_store_append(list, f);
    g_list_store_append(list, all);
    g_object_unref(f); g_object_unref(all);
    return G_LIST_MODEL(list);
}
static char *chosen_path(GObject *dialog, GAsyncResult *res, int folder) {
    GFile *f = folder ? gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(dialog), res, NULL)
                      : gtk_file_dialog_open_finish(GTK_FILE_DIALOG(dialog), res, NULL);
    char *path = f ? g_file_get_path(f) : NULL;
    if (f) g_object_unref(f);
    return path;
}
static void image_chosen(GObject *d, GAsyncResult *r, gpointer u) { (void)u; char *p = chosen_path(d, r, 0); if (p) use_image(p); g_free(p); }
static void folder_chosen(GObject *d, GAsyncResult *r, gpointer u) { (void)u; char *p = chosen_path(d, r, 1); if (p) use_folder(p); g_free(p); }
static void soundfont_chosen(GObject *d, GAsyncResult *r, gpointer u) {
    (void)u;
    char *p = chosen_path(d, r, 0);
    if (p) { g_strlcpy(g_soundfont, p, sizeof g_soundfont); save_settings(); say("The music will be played with that SoundFont."); update(); }
    g_free(p);
}

static void choose_image(GSimpleAction *a, GVariant *v, gpointer u) {
    (void)a; (void)v; (void)u;
    static const char *const iso[] = { "*.iso", "*.ISO", NULL }, *const iso_zip[] = { "*.iso", "*.ISO", "*.zip", "*.ZIP", NULL };
    GtkFileDialog *d = gtk_file_dialog_new();
    GListModel *f = g_kit[0] ? filters("Disc images (.iso, .zip)", iso_zip) : filters("Disc images (.iso)", iso);
    gtk_file_dialog_set_title(d, "Choose your own disc image of the game");
    gtk_file_dialog_set_filters(d, f);
    gtk_file_dialog_open(d, g_win, NULL, image_chosen, NULL);
    g_object_unref(f); g_object_unref(d);
}
static void choose_folder(GSimpleAction *a, GVariant *v, gpointer u) {
    (void)a; (void)v; (void)u;
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, "Choose the folder that holds the disc's files (it contains MADEURO.EXE)");
    gtk_file_dialog_select_folder(d, g_win, NULL, folder_chosen, NULL);
    g_object_unref(d);
}
static void choose_soundfont(GtkButton *b, gpointer u) {
    (void)b; (void)u;
    static const char *const sf[] = { "*.sf2", "*.SF2", "*.sf3", "*.SF3", NULL };
    GtkFileDialog *d = gtk_file_dialog_new();
    GListModel *f = filters("SoundFonts (.sf2, .sf3)", sf);
    gtk_file_dialog_set_title(d, "Choose a General MIDI SoundFont for the music");
    gtk_file_dialog_set_filters(d, f);
    gtk_file_dialog_open(d, g_win, NULL, soundfont_chosen, NULL);
    g_object_unref(f); g_object_unref(d);
}
static void system_soundfont(GtkButton *b, gpointer u) {
    (void)b; (void)u;
    g_soundfont[0] = 0;
    save_settings();
    say("");
    update();
}
static void open_saves(GtkButton *b, gpointer u) {
    (void)b; (void)u;
    char dir[PATH_MAX + 32];
    snprintf(dir, sizeof dir, "%s/CWONDERS/MADEURO", g_data);
    g_mkdir_with_parents(dir, 0755);
    GFile *f = g_file_new_for_path(dir);
    GtkFileLauncher *l = gtk_file_launcher_new(f);
    gtk_file_launcher_launch(l, g_win, NULL, NULL, NULL);
    g_object_unref(l); g_object_unref(f);
}
static void on_play(GtkButton *b, gpointer u) { (void)b; (void)u; play(); }
static void on_build(GtkButton *b, gpointer u) { (void)b; (void)u; build_engine(); }
static void on_toggle(GtkCheckButton *b, gpointer u) { (void)b; (void)u; save_settings(); update(); }
static void on_select(GObject *o, GParamSpec *p, gpointer u) { (void)o; (void)p; (void)u; save_settings(); update(); }

/* ---- drawing ------------------------------------------------------------ */

/* The hat of launcher/icon.svg (the same drawing as make_icon.swift): a
 * straw boater, a little tilted, with a dark band and two ribbon tails. */
static void ellipse(cairo_t *cr, double cx, double cy, double rx, double ry) {
    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    cairo_scale(cr, rx, ry);
    cairo_arc(cr, 0, 0, 1, 0, 2 * G_PI);
    cairo_restore(cr);
}
static void rounded(cairo_t *cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}
static void quad(cairo_t *cr, double x0, double y0, double cx, double cy, double x1, double y1) {
    cairo_move_to(cr, x0, y0);
    cairo_curve_to(cr, x0 + 2.0 / 3 * (cx - x0), y0 + 2.0 / 3 * (cy - y0), x1 + 2.0 / 3 * (cx - x1), y1 + 2.0 / 3 * (cy - y1), x1, y1);
}
static void draw_hat(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer u) {
    (void)area; (void)u;
    const double ink[] = { 0.10, 0.12, 0.22 }, straw[] = { 0.99, 0.82, 0.18 }, under[] = { 0.93, 0.66, 0.10 };
    double s = MIN(w / 680.0, h / 500.0);               /* the hat spans about 680 x 500 of the icon's 1024 */
    cairo_translate(cr, w / 2.0, h / 2.0);
    cairo_scale(cr, s, s);
    cairo_rotate(cr, -0.14);
    cairo_translate(cr, -512, -560);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_source_rgb(cr, ink[0], ink[1], ink[2]);  /* ribbon tails, behind the brim */
    cairo_set_line_width(cr, 40);
    quad(cr, 668, 560, 770, 610, 800, 742);
    quad(cr, 640, 575, 700, 660, 704, 782);
    cairo_stroke(cr);
    for (int front = 0; front < 2; front++) {           /* the brim, then its front half again over the crown */
        cairo_save(cr);
        if (front) { cairo_rectangle(cr, 0, 588, 1024, 1024); cairo_clip(cr); }
        ellipse(cr, 512, 588, 316, 88);
        cairo_set_source_rgb(cr, straw[0], straw[1], straw[2]);
        cairo_fill_preserve(cr);
        cairo_save(cr);
        cairo_clip(cr);
        ellipse(cr, 512, 648, 316, 88);                 /* the underside showing at the front */
        cairo_set_source_rgb(cr, under[0], under[1], under[2]);
        cairo_fill(cr);
        if (!front) { ellipse(cr, 512, 574, 306, 82); cairo_set_source_rgb(cr, straw[0], straw[1], straw[2]); cairo_fill(cr); }
        cairo_restore(cr);
        ellipse(cr, 512, 588, 316, 88);
        cairo_set_source_rgb(cr, ink[0], ink[1], ink[2]);
        cairo_set_line_width(cr, 22);
        cairo_stroke(cr);
        cairo_restore(cr);
        if (front) break;
        rounded(cr, 356, 336, 312, 250, 70);            /* the crown, its band and a highlight */
        cairo_set_source_rgb(cr, straw[0], straw[1], straw[2]);
        cairo_fill_preserve(cr);
        cairo_save(cr);
        cairo_clip(cr);
        cairo_set_source_rgb(cr, ink[0], ink[1], ink[2]);
        cairo_rectangle(cr, 340, 498, 344, 60);
        cairo_fill(cr);
        ellipse(cr, 461, 387, 75, 35);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.28);
        cairo_fill(cr);
        cairo_restore(cr);
        rounded(cr, 356, 336, 312, 250, 70);
        cairo_set_source_rgb(cr, ink[0], ink[1], ink[2]);
        cairo_stroke(cr);
    }
}

/* A picture of the window, as a PNG, once nothing is pending; then quit. */
static gboolean snapshot_when_settled(gpointer app) {
    static int ticks;
    if (++ticks < 3 || g_busy || g_building || (disc_valid() && g_engine_ok < 0)) return G_SOURCE_CONTINUE;
    GtkWidget *w = GTK_WIDGET(g_win);
    int W = gtk_widget_get_width(w), H = gtk_widget_get_height(w);
    GdkPaintable *paint = gtk_widget_paintable_new(w);
    GtkSnapshot *snap = gtk_snapshot_new();
    gdk_paintable_snapshot(paint, snap, W, H);
    GskRenderNode *node = gtk_snapshot_free_to_node(snap);
    if (node) {
        GdkTexture *t = gsk_renderer_render_texture(gtk_native_get_renderer(GTK_NATIVE(w)), node, &GRAPHENE_RECT_INIT(0, 0, W, H));
        if (!gdk_texture_save_to_png(t, g_snapshot)) fprintf(stderr, "launcher: could not write %s\n", g_snapshot);
        g_object_unref(t);
        gsk_render_node_unref(node);
    }
    g_object_unref(paint);
    g_application_quit(G_APPLICATION(app));
    return G_SOURCE_REMOVE;
}

/* ---- the window --------------------------------------------------------- */

static const char *CSS =
    ".header { background-image: linear-gradient(to bottom, #ffe685, #fff5cc); padding: 10px 24px 12px 20px; }\n"
    ".header .title { font-family: Georgia, 'DejaVu Serif', serif; font-size: 23px; font-weight: bold; color: #1a1f38; }\n"
    ".header .subtitle { font-family: Georgia, 'DejaVu Serif', serif; font-size: 17px; font-weight: bold; font-style: italic; color: #294d9e; }\n"
    ".header .tagline { color: #505464; }\n"
    ".section { font-weight: bold; margin-top: 6px; }\n"
    ".hint { opacity: 0.65; }\n"
    ".ok { color: #2a9a4c; }\n"
    ".warn { color: #c87410; }\n"
    ".save-name { font-weight: 600; }\n"
    "list.saves row { padding: 4px 10px; }\n"
    "button.play { background: #facc29; color: #1a1f38; font-size: 15px; font-weight: 600; border: 1px solid #1a1f38;"
    "  border-radius: 10px; padding: 6px 22px; box-shadow: none; }\n"
    "button.play:hover { background: #ffd84a; }\n"
    "button.play:active { background: #e8b814; }\n"
    "button.play:disabled { opacity: 0.45; }\n";

static GtkWidget *label_with(const char *text, const char *css, float xalign) {
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    if (css) gtk_widget_add_css_class(l, css);
    return l;
}
static GtkWidget *wrapping(GtkWidget *l) {
    gtk_label_set_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(l), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 48);    /* about one column of the window */
    return l;
}
static GtkWidget *checkbox(GtkWidget *box, const char *text) {
    GtkWidget *c = gtk_check_button_new_with_label(text);
    gtk_box_append(GTK_BOX(box), c);
    g_signal_connect(c, "toggled", G_CALLBACK(on_toggle), NULL);
    return c;
}
static GtkWidget *vbox(int spacing) { return gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing); }
static GtkWidget *hbox(int spacing) { return gtk_box_new(GTK_ORIENTATION_HORIZONTAL, spacing); }
/* A status block on the left and a button on the right. */
static GtkWidget *row_with_button(GtkWidget *left, GtkWidget *button) {
    GtkWidget *r = hbox(12);
    gtk_widget_set_hexpand(left, TRUE);
    gtk_widget_set_valign(button, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(r), left);
    gtk_box_append(GTK_BOX(r), button);
    return r;
}

static GtkWidget *header(void) {
    GtkWidget *h = hbox(16), *hat = gtk_drawing_area_new(), *text = vbox(0);
    gtk_widget_add_css_class(h, "header");
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(hat), 84);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(hat), 70);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(hat), draw_hat, NULL, NULL);
    gtk_box_append(GTK_BOX(h), hat);
    gtk_box_append(GTK_BOX(text), label_with("The Little Girl in the Yellow Hat", "title", 0));
    gtk_box_append(GTK_BOX(text), label_with("Across Europe", "subtitle", 0));
    gtk_box_append(GTK_BOX(text), label_with("A 1996 adventure, running natively. Bring your own disc.", "tagline", 0));
    gtk_widget_set_valign(text, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(h), text);
    return h;
}

static void build_window(GtkApplication *app) {
    GtkWidget *win = gtk_application_window_new(app), *outer = vbox(0), *body = hbox(32), *b, *col;
    g_win = GTK_WINDOW(win);
    gtk_window_set_title(g_win, TITLE);
    gtk_window_set_icon_name(g_win, APP_ID);
    gtk_window_set_default_size(g_win, 880, -1);
    gtk_window_set_resizable(g_win, FALSE);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, CSS);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    gtk_box_append(GTK_BOX(outer), header());
    gtk_box_append(GTK_BOX(outer), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_widget_set_margin_start(body, 24); gtk_widget_set_margin_end(body, 24);
    gtk_widget_set_margin_top(body, 12); gtk_widget_set_margin_bottom(body, 12);
    gtk_box_append(GTK_BOX(outer), body);

    /* Two columns, so the window is wide rather than tall and fits a small screen:
     * the disc, its music and the saves on the left; how it starts and looks on the right. */
    GtkWidget *left = vbox(8), *right = vbox(8);
    gtk_box_set_homogeneous(GTK_BOX(body), TRUE);
    gtk_box_append(GTK_BOX(body), left);
    gtk_box_append(GTK_BOX(body), right);

    /* The disc, and the engine made from it. */
    gtk_box_append(GTK_BOX(left), label_with("Game disc", "section", 0));
    b = vbox(2);
    gtk_box_append(GTK_BOX(b), g_disc_status = label_with("", NULL, 0));
    gtk_box_append(GTK_BOX(b), g_disc_path = label_with("", "hint", 0));
    gtk_label_set_ellipsize(GTK_LABEL(g_disc_path), PANGO_ELLIPSIZE_MIDDLE);
    GMenu *menu = g_menu_new();
    g_menu_append(menu, g_kit[0] ? "Disc image (.iso or .zip)…" : "Disc image (.iso)…", "app.choose-image");
    g_menu_append(menu, "Folder with the disc's files…", "app.choose-folder");
    g_choose = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(g_choose), "Choose…");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(g_choose), G_MENU_MODEL(menu));
    g_object_unref(menu);
    gtk_box_append(GTK_BOX(left), row_with_button(b, g_choose));
    b = vbox(2);
    gtk_box_append(GTK_BOX(b), g_engine_status = label_with("", NULL, 0));
    gtk_box_append(GTK_BOX(b), g_engine_hint = wrapping(label_with("The game's program is translated to native code from your disc, on this computer.", "hint", 0)));
    g_build = gtk_button_new_with_label("Build");
    g_signal_connect(g_build, "clicked", G_CALLBACK(on_build), NULL);
    gtk_box_append(GTK_BOX(left), row_with_button(b, g_build));

    /* The music's SoundFont: Linux has no General MIDI sound bank of its own. */
    gtk_box_append(GTK_BOX(left), label_with("Music", "section", 0));
    g_sf_status = wrapping(label_with("", NULL, 0));
    GtkWidget *sfb = hbox(6);
    g_sf_reset = gtk_button_new_with_label("Use the system's");
    g_sf_choose = gtk_button_new_with_label("SoundFont…");
    g_signal_connect(g_sf_reset, "clicked", G_CALLBACK(system_soundfont), NULL);
    g_signal_connect(g_sf_choose, "clicked", G_CALLBACK(choose_soundfont), NULL);
    gtk_box_append(GTK_BOX(sfb), g_sf_reset);
    gtk_box_append(GTK_BOX(sfb), g_sf_choose);
    gtk_box_append(GTK_BOX(left), row_with_button(g_sf_status, sfb));

    /* The saved games. */
    GtkWidget *open = gtk_button_new_with_label("Open folder");
    g_signal_connect(open, "clicked", G_CALLBACK(open_saves), NULL);
    gtk_box_append(GTK_BOX(left), row_with_button(label_with("Saved games", "section", 0), open));
    g_saves = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_saves), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(g_saves, "saves");
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), g_saves);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_widget_set_size_request(scroll, -1, 104);
    gtk_widget_set_vexpand(scroll, TRUE);          /* the saves take what the right column leaves */
    gtk_box_append(GTK_BOX(left), scroll);
    gtk_box_append(GTK_BOX(left), wrapping(label_with("Each name signed into the book has its own game: sign in with it again and choose “old game”.", "hint", 0)));

    /* How it starts, and how it looks. */
    gtk_box_append(GTK_BOX(right), label_with("Starting up", "section", 0));
    col = vbox(2);
    g_skip = checkbox(col, "Skip the opening movies");
    g_name = checkbox(col, "Fill in my name on the sign-in book");
    g_full = checkbox(col, "Start in full screen");
    gtk_box_append(GTK_BOX(right), col);
    gtk_box_append(GTK_BOX(right), label_with("Picture", "section", 0));
    col = vbox(2);
    g_smooth = checkbox(col, "Smooth the painting");
    g_xbr = checkbox(col, "Redraw edges at twice the size (xBR)");
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    g_scale = gtk_drop_down_new_from_strings(SCALE_LABELS);
    g_bars = gtk_drop_down_new_from_strings(BARS_LABELS);
    gtk_widget_set_hexpand(g_scale, TRUE);
    gtk_grid_attach(GTK_GRID(grid), label_with("Scaling", NULL, 0), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), g_scale, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_with("Beside the picture", NULL, 0), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), g_bars, 1, 1, 1, 1);
    gtk_widget_set_margin_top(grid, 4);
    gtk_box_append(GTK_BOX(col), grid);
    gtk_box_append(GTK_BOX(right), col);
    gtk_box_append(GTK_BOX(right), wrapping(label_with("Smoothing melts the 256-colour speckle into washes of paint; xBR takes the stair-steps out of slanted lines.", "hint", 0)));

    /* The footer: what is going on, and Play. */
    gtk_box_append(GTK_BOX(outer), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    GtkWidget *foot = hbox(12);
    gtk_widget_set_margin_start(foot, 24); gtk_widget_set_margin_end(foot, 24);
    gtk_widget_set_margin_top(foot, 12); gtk_widget_set_margin_bottom(foot, 14);
    g_status = wrapping(label_with("", "hint", 0));
    gtk_widget_set_hexpand(g_status, TRUE);
    gtk_widget_set_valign(g_status, GTK_ALIGN_CENTER);
    g_play = gtk_button_new_with_label("▶  Play");
    gtk_widget_add_css_class(g_play, "play");
    g_signal_connect(g_play, "clicked", G_CALLBACK(on_play), NULL);
    gtk_box_append(GTK_BOX(foot), g_status);
    gtk_box_append(GTK_BOX(foot), g_play);
    gtk_box_append(GTK_BOX(outer), foot);

    gtk_window_set_child(g_win, outer);
    gtk_window_set_default_widget(g_win, g_play);       /* Enter presses Play */

    load_settings(1);
    g_signal_connect(g_scale, "notify::selected", G_CALLBACK(on_select), NULL);
    g_signal_connect(g_bars, "notify::selected", G_CALLBACK(on_select), NULL);
    load_saves();
}

static char g_given[PATH_MAX];         /* a disc image or folder named on the command line */

static void activate(GtkApplication *app, gpointer u) {
    (void)u;
    if (g_win) { gtk_window_present(g_win); return; }
    build_window(app);
    update();
    if (g_given[0]) {
        if (g_file_test(g_given, G_FILE_TEST_IS_DIR)) use_folder(g_given); else use_image(g_given);
    } else check_engine(0);
    gtk_window_present(g_win);
    gtk_widget_grab_focus(g_play);
    if (g_snapshot[0]) g_timeout_add(400, snapshot_when_settled, app);
}

/* One line of a small text file beside this program (written by `make app`). */
static void read_note(const char *dir, const char *name, char *out, size_t n) {
    char path[PATH_MAX + 64], *text = NULL;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    out[0] = 0;
    if (!g_file_get_contents(path, &text, NULL, NULL)) return;
    text[strcspn(text, "\r\n")] = 0;
    g_strlcpy(out, text, n);
    g_free(text);
}

int main(int argc, char **argv) {
    char here[PATH_MAX], makefile[PATH_MAX + 16];
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--snapshot") && i + 1 < argc) g_strlcpy(g_snapshot, argv[++i], sizeof g_snapshot);
        else if (argv[i][0] != '-' && !realpath(argv[i], g_given)) g_given[0] = 0;
    }

    /* Where things are: the data folder is the engine's (src/platform.c). */
    const char *save = getenv("MADELINE_SAVE_DIR"), *xdg = getenv("XDG_DATA_HOME");
    if (save && *save) g_strlcpy(g_data, save, sizeof g_data);
    else if (xdg && *xdg) snprintf(g_data, sizeof g_data, "%s/madeline-european-adventures", xdg);
    else snprintf(g_data, sizeof g_data, "%s/.local/share/madeline-european-adventures", g_get_home_dir());
    snprintf(g_ini, sizeof g_ini, "%s/madeline.ini", g_data);
    ssize_t n = readlink("/proc/self/exe", here, sizeof here - 1);
    if (n <= 0) g_strlcpy(here, ".", sizeof here); else { here[n] = 0; *strrchr(here, '/') = 0; }
    const char *engine = getenv("MADELINE_ENGINE");
    if (engine && *engine) g_strlcpy(g_engine, engine, sizeof g_engine);
    else snprintf(g_engine, sizeof g_engine, "%s/madeline", here);
    read_note(here, "source-root.txt", g_root, sizeof g_root);
    snprintf(makefile, sizeof makefile, "%s/Makefile", g_root);
    if (!file_exists(makefile)) g_root[0] = 0;
    const char *kit = getenv("MADELINE_KIT");
    char kit_json[PATH_MAX + 96];
    if (kit && *kit) g_strlcpy(g_kit, kit, sizeof g_kit);
    else snprintf(g_kit, sizeof g_kit, "%s/../lib/yellow-hat-across-europe/kit", here);
    snprintf(kit_json, sizeof kit_json, "%s/kit.json", g_kit);
    if (file_exists(kit_json)) {       /* a release: the engine lives in the data folder */
        snprintf(g_kit_python, sizeof g_kit_python, "%s/python/bin/python3", g_kit);
        snprintf(g_kit_tool, sizeof g_kit_tool, "%s/madeline_kit.py", g_kit);
        snprintf(g_engine_dir, sizeof g_engine_dir, "%s/engine", g_data);
        if (!(engine && *engine)) snprintf(g_engine, sizeof g_engine, "%s/madeline", g_engine_dir);
    } else g_kit[0] = 0;

    /* A snapshot run is its own instance; otherwise a second start raises the first window. */
    GtkApplication *app = gtk_application_new(APP_ID, g_snapshot[0] ? G_APPLICATION_NON_UNIQUE : G_APPLICATION_DEFAULT_FLAGS);
    static const GActionEntry actions[] = { { .name = "choose-image", .activate = choose_image }, { .name = "choose-folder", .activate = choose_folder } };
    g_action_map_add_action_entries(G_ACTION_MAP(app), actions, G_N_ELEMENTS(actions), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int rc = g_application_run(G_APPLICATION(app), 1, argv);   /* the arguments are this program's, read above */
    g_object_unref(app);
    return rc;
}
