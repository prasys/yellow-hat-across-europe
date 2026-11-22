/*
 * launcher.c - the Windows launcher: choose the disc, set how the game looks
 * and starts, see the saved games, press Play. The same job as the SwiftUI
 * one beside it (the .swift files), on plain Win32 so it builds with the
 * compiler the engine already needs and depends on nothing else.
 *
 * All drawing here is the launcher's own (a yellow hat made of two shapes);
 * nothing is taken from the game.
 *
 * It edits madeline.ini, the "key=value" file the engine (src/main.c) reads
 * and writes, changing only the keys it knows and leaving every other line as
 * it found it.
 *
 *   launcher [--snapshot FILE.bmp] [disc image or folder]
 *
 * --snapshot writes a picture of the window once it has settled and quits
 * (for checking the layout). MADELINE_SAVE_DIR moves the settings and saves,
 * as it does for the engine: tests use it so they never touch a player's own.
 *
 * A release (the zip of scripts/package/windows.sh) carries no engine: the
 * engine is the game's code and is built on the player's machine by the kit
 * beside this program (kit\madeline_kit.py, MADELINE_KIT to name another),
 * into the data folder. With a kit, unpacking, checking and building go
 * through it; without one (a build from a checkout) through MSYS2's make.
 */
#include <windows.h>      /* built with -municode: the wide API throughout */
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define TITLE L"The Little Girl in the Yellow Hat Across Europe"

enum {
    ID_PLAY = 1,                       /* IDOK: Enter presses it */
    ID_CHOOSE = 100, ID_BUILD, ID_OPEN, ID_SKIP, ID_NAME, ID_FULL, ID_SMOOTH, ID_XBR, ID_PAD, ID_PTR, ID_SCALE, ID_BARS, ID_SAVES,
    ID_DISC_STATUS = 200, ID_ENGINE_STATUS, ID_STATUS,
    ID_HINT = 300, ID_TEXT = 400,
    IDM_IMAGE = 500, IDM_FOLDER,
};
enum { WM_UNPACKED = WM_APP + 1, WM_CHECKED, WM_BUILT, WM_PLAYED };
enum { TIMER_SNAPSHOT = 1 };

#define INK      RGB(26, 31, 56)
#define COAT     RGB(41, 77, 158)
#define HAT      RGB(250, 204, 41)
#define PAPER_T  RGB(255, 230, 133)
#define PAPER_B  RGB(255, 245, 204)
#define GREY     RGB(110, 110, 110)
#define GREEN    RGB(30, 130, 60)
#define ORANGE   RGB(190, 100, 0)

static const wchar_t *const SCALE_KEYS[] = { L"smooth", L"sharp", L"crt", L"nearest", L"integer" };
static const wchar_t *const SCALE_LABELS[] = { L"Smooth", L"Sharp", L"CRT monitor", L"Blocky pixels", L"Whole multiples only" };
#define FOOT_Y 480       /* the rule above the footer; the window is 880 x (FOOT_Y + 62) */
#define KEYS_HINT L"F8 xBR  \u00B7  F9 screenshot  \u00B7  F10 smoothing  \u00B7  F11 full screen  \u00B7  F12 scaling"

static HWND g_wnd, g_disc_status, g_disc_path, g_engine_status, g_engine_hint, g_choose, g_build, g_open,
            g_skip, g_name, g_full, g_smooth, g_xbr, g_pad, g_ptr, g_scale, g_bars, g_saves, g_status, g_play;
static HFONT g_font, g_bold, g_title, g_subtitle, g_playfont;
static HBRUSH g_white;
static int g_dpi = 96;

static wchar_t g_data[MAX_PATH];       /* settings, saves, screenshots: the engine's folder (src/platform.c) */
static wchar_t g_ini[MAX_PATH];
static wchar_t g_engine[MAX_PATH];     /* madeline.exe: beside this program, or a release's in the data folder */
static wchar_t g_root[MAX_PATH];       /* the checkout this was built from, for building an engine */
static wchar_t g_msys[MAX_PATH];       /* the MSYS2 it was built with */
static wchar_t g_kit[MAX_PATH];        /* a release's kit, or empty */
static wchar_t g_disc[MAX_PATH];
static wchar_t g_message[512];         /* what the footer says; empty: the key hints */
static wchar_t g_snapshot[MAX_PATH];
static int g_busy, g_building, g_playing, g_then_build;
static int g_engine_ok = -1;           /* was the engine recompiled from this disc's MADEURO.EXE? -1: not known yet */

static int S(int v) { return MulDiv(v, g_dpi, 96); }
static int file_exists(const wchar_t *p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
static int disc_valid(void) {
    wchar_t exe[MAX_PATH + 16];
    if (!g_disc[0]) return 0;
    swprintf(exe, MAX_PATH + 16, L"%ls\\MADEURO.EXE", g_disc);
    return file_exists(exe);
}
static void say(const wchar_t *text) { lstrcpynW(g_message, text, 512); }

/* ---- madeline.ini ------------------------------------------------------- */

#define INI_LINES 256
static char g_lines[INI_LINES][1024];
static int g_nlines;

static void ini_load(void) {
    FILE *f = _wfopen(g_ini, L"rb");
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
    return atoi(v) != 0 || !_stricmp(v, "yes") || !_stricmp(v, "true") || !_stricmp(v, "on");
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
    wchar_t tmp[MAX_PATH + 8];
    FILE *f;
    SHCreateDirectoryExW(NULL, g_data, NULL);
    swprintf(tmp, MAX_PATH + 8, L"%ls.new", g_ini);
    if (!(f = _wfopen(tmp, L"wb"))) return 0;
    for (int i = 0; i < g_nlines; i++) fprintf(f, "%s\n", g_lines[i]);
    fclose(f);
    return MoveFileExW(tmp, g_ini, MOVEFILE_REPLACE_EXISTING) != 0;
}

static int checked(HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }
static void check(HWND h, int on) { SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); }

/* The window's settings into the file. The engine opens files by the system
 * code page, so the disc's path is written in it too. */
static void save_settings(void) {
    char cd[MAX_PATH * 2], scale[32];
    int s = (int)SendMessageW(g_scale, CB_GETCURSEL, 0, 0);
    ini_load();
    WideCharToMultiByte(CP_ACP, 0, g_disc, -1, cd, sizeof cd, NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, SCALE_KEYS[s < 0 ? 0 : s], -1, scale, sizeof scale, NULL, NULL);
    ini_set("cd", cd);
    ini_set("skip_intro", checked(g_skip) ? "1" : "0");
    ini_set("fullscreen", checked(g_full) ? "1" : "0");
    ini_set("remember_name", checked(g_name) ? "1" : "0");
    ini_set("smooth", checked(g_smooth) ? "1" : "0");
    ini_set("upscale", checked(g_xbr) ? "xbr" : "off");
    ini_set("gamepad", checked(g_pad) ? "1" : "0");
    ini_set("pointer", checked(g_ptr) ? "game" : "plain");
    ini_set("scale", scale);
    ini_set("bars", SendMessageW(g_bars, CB_GETCURSEL, 0, 0) == 1 ? "black" : "blur");
    if (!ini_write()) say(L"Could not save the settings.");
}
/* The file into the window; also after a game, which may have changed it (F10, F11, F12). */
static void load_settings(int first) {
    char v[1024];
    ini_load();
    if (first) {
        if (ini_get("cd", v, sizeof v)) MultiByteToWideChar(CP_ACP, 0, v, -1, g_disc, MAX_PATH);
        for (wchar_t *p = g_disc; *p; p++) if (*p == L'/') *p = L'\\';
        check(g_skip, ini_flag("skip_intro", 0));
        check(g_name, ini_flag("remember_name", 1));
        check(g_pad, ini_flag("gamepad", 1));
        check(g_ptr, ini_get("pointer", v, sizeof v) && !strcmp(v, "game"));
    }
    check(g_full, ini_flag("fullscreen", 0));
    check(g_smooth, ini_flag("smooth", 1));
    check(g_xbr, ini_get("upscale", v, sizeof v) && !strcmp(v, "xbr"));
    int s = 0;
    if (ini_get("scale", v, sizeof v))
        for (int i = 0; i < 5; i++) { char k[32]; WideCharToMultiByte(CP_ACP, 0, SCALE_KEYS[i], -1, k, sizeof k, NULL, NULL); if (!strcmp(v, k)) s = i; }
    SendMessageW(g_scale, CB_SETCURSEL, s, 0);
    SendMessageW(g_bars, CB_SETCURSEL, ini_get("bars", v, sizeof v) && !strcmp(v, "black") ? 1 : 0, 0);
}

/* ---- the saved games ---------------------------------------------------- */

typedef struct { wchar_t name[20]; FILETIME when; } Save;
static int newer_first(const void *a, const void *b) { return CompareFileTime(&((const Save *)b)->when, &((const Save *)a)->when); }

/* A save begins with the player's name (NUL-terminated, 16 bytes); a non-zero
 * word at 0xF0 marks a file the game discards (docs/NOTES.md). */
static void load_saves(void) {
    Save saves[10];
    int n = 0;
    for (int slot = 0; slot < 10; slot++) {
        wchar_t path[MAX_PATH + 40];
        unsigned char head[0xF2];
        DWORD got = 0;
        swprintf(path, MAX_PATH + 40, L"%ls\\CWONDERS\\MADEURO\\%d.sav", g_data, slot);
        HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) continue;
        if (ReadFile(f, head, sizeof head, &got, NULL) && got == sizeof head && !head[0xF0] && !head[0xF1] && head[0]) {
            char name[16];
            memcpy(name, head, 15); name[15] = 0;
            MultiByteToWideChar(1252, 0, name, -1, saves[n].name, 20);
            GetFileTime(f, NULL, NULL, &saves[n].when);
            n++;
        }
        CloseHandle(f);
    }
    qsort(saves, n, sizeof saves[0], newer_first);
    SendMessageW(g_saves, LVM_DELETEALLITEMS, 0, 0);
    for (int i = 0; i < n; i++) {
        SYSTEMTIME utc, local;
        wchar_t date[64], time[64], when[140];
        LVITEMW it = { .mask = LVIF_TEXT, .iItem = i, .pszText = saves[i].name };
        SendMessageW(g_saves, LVM_INSERTITEMW, 0, (LPARAM)&it);
        FileTimeToSystemTime(&saves[i].when, &utc);
        SystemTimeToTzSpecificLocalTime(NULL, &utc, &local);
        GetDateFormatW(LOCALE_USER_DEFAULT, DATE_LONGDATE, &local, NULL, date, 64);
        GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &local, NULL, time, 64);
        swprintf(when, 140, L"%ls, %ls", date, time);
        it.iSubItem = 1; it.pszText = when;
        SendMessageW(g_saves, LVM_SETITEMW, 0, (LPARAM)&it);
    }
    if (!n) {
        LVITEMW it = { .mask = LVIF_TEXT, .pszText = L"None yet. In the game, MENU then QUIT saves." };
        SendMessageW(g_saves, LVM_INSERTITEMW, 0, (LPARAM)&it);
    }
}

/* ---- what the window shows ---------------------------------------------- */

static int can_build(void) { return ((g_root[0] && g_msys[0]) || g_kit[0]) && disc_valid() && !g_busy && !g_building && !g_playing; }
static int can_play(void) { return disc_valid() && g_engine_ok == 1 && !g_busy && !g_building && !g_playing && file_exists(g_engine); }

static void update(void) {
    int valid = disc_valid();
    SetWindowTextW(g_disc_status, g_busy ? L"Unpacking the disc image\u2026" : valid ? L"\u2713  Disc found" : L"No disc chosen yet");
    SetWindowTextW(g_disc_path, g_disc[0] ? g_disc : g_kit[0] ? L"Choose your own disc image (.iso, or the .zip it came in) or a folder with the disc's files."
                                                          : L"Choose your own disc image (.iso) or a folder with the disc's files.");
    SetWindowTextW(g_engine_status, g_building ? L"Recompiling the game from your disc\u2026"
                   : g_engine_ok == 1 ? L"\u2713  Engine built for this disc"
                   : g_engine_ok == 0 ? L"This disc needs its engine built" : L"Checking the engine\u2026");
    ShowWindow(g_engine_status, valid ? SW_SHOW : SW_HIDE);
    ShowWindow(g_engine_hint, valid ? SW_SHOW : SW_HIDE);
    ShowWindow(g_build, valid && g_engine_ok == 0 && !g_building ? SW_SHOW : SW_HIDE);
    EnableWindow(g_build, can_build());
    EnableWindow(g_choose, !g_busy && !g_building && !g_playing);
    EnableWindow(g_play, can_play());
    SetWindowTextW(g_play, g_playing ? L"Playing" : L"\u25B6  Play");
    SetWindowTextW(g_status, g_message[0] ? g_message : KEYS_HINT);
}

/* ---- other programs: the engine, the build, the unpacking ---------------- */

typedef struct { wchar_t cmd[4096], cwd[MAX_PATH], log[MAX_PATH]; UINT msg; } Job;

/* Run a program without a console window and post its exit code (-1: it would not start). */
static DWORD WINAPI job_thread(void *arg) {
    Job *job = arg;
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    STARTUPINFOW si = { .cb = sizeof si, .dwFlags = STARTF_USESTDHANDLES };
    PROCESS_INFORMATION pi;
    DWORD code = (DWORD)-1;
    HANDLE out = CreateFileW(job->log[0] ? job->log : L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, 0, NULL);
    HANDLE in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    si.hStdInput = in; si.hStdOutput = out; si.hStdError = out;
    if (CreateProcessW(NULL, job->cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, job->cwd[0] ? job->cwd : NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
    PostMessageW(g_wnd, job->msg, code, 0);
    free(job);
    return 0;
}
static Job *new_job(UINT msg) { Job *j = calloc(1, sizeof *j); j->msg = msg; return j; }
static void start_job(Job *job) { HANDLE t = CreateThread(NULL, 0, job_thread, job, 0, NULL); if (t) CloseHandle(t); }

/* C:\a\b as MSYS2's make wants it: /c/a/b. */
static void msys_path(const wchar_t *in, wchar_t *out, size_t n) {
    if (in[0] && in[1] == L':') swprintf(out, n, L"/%lc%ls", towlower(in[0]), in + 2);
    else swprintf(out, n, L"%ls", in);
    for (wchar_t *p = out; *p; p++) if (*p == L'\\') *p = L'/';
}

/* A path as one argument of a command line: in quotes, with any backslashes
 * before the closing quote doubled, or a disc at a drive's root ("D:\\")
 * would end in \\" and swallow the quote (CommandLineToArgvW's rule). */
static const wchar_t *arg(const wchar_t *path, wchar_t *out) {
    size_t n = wcslen(path), tail = 0;
    while (tail < n && path[n - 1 - tail] == L'\\') tail++;
    swprintf(out, 2 * MAX_PATH, L"\"%ls%.*ls\"", path, (int)tail, path + n - tail);
    return out;
}

/* The kit's tool, run by the kit's own Python. */
static void kit_cmd(Job *job, const wchar_t *args) {
    swprintf(job->cmd, 4096, L"\"%ls\\python\\python.exe\" -IB \"%ls\\madeline_kit.py\" %ls", g_kit, g_kit, args);
}
static void build_log(wchar_t *out) {
    if (g_kit[0]) swprintf(out, MAX_PATH, L"%ls\\engine-build.log", g_data);
    else swprintf(out, MAX_PATH, L"%ls\\build\\launcher-build.log", g_root);
}

/* The engine is the game's own code, lifted from one particular MADEURO.EXE:
 * a disc with a different build needs its own. `make engine-into-app` lifts
 * this disc's, compiles it with the host and puts it beside this program; in
 * a release the kit does the same into the data folder. */
static void build_engine(void) {
    wchar_t root[MAX_PATH], disc[MAX_PATH], args[3 * MAX_PATH];
    if (g_kit[0]) {
        Job *job = new_job(WM_BUILT);
        wchar_t a[2 * MAX_PATH];
        swprintf(args, 3 * MAX_PATH, L"build %ls \"%ls\\engine\"", arg(g_disc, a), g_data);
        kit_cmd(job, args);
        build_log(job->log);
        SHCreateDirectoryExW(NULL, g_data, NULL);
        g_building = 1;
        say(L"Recompiling the game from your disc\u2026 (one to three minutes the first time)");
        update();
        start_job(job);
        return;
    }
    if (!g_root[0] || !g_msys[0]) {
        say(L"This disc needs its own engine, and this program does not know where its source is. Run: make app CD=<disc folder>");
        update();
        return;
    }
    Job *job = new_job(WM_BUILT);
    msys_path(g_root, root, MAX_PATH);
    msys_path(g_disc, disc, MAX_PATH);
    swprintf(job->cmd, 4096, L"\"%ls\\usr\\bin\\bash.exe\" -lc \"make -C '%ls' engine-into-app CD='%ls'\"", g_msys, root, disc);
    build_log(job->log);
    SetEnvironmentVariableW(L"MSYSTEM", L"UCRT64");
    g_building = 1;
    say(L"Recompiling the game from your disc\u2026");
    update();
    start_job(job);
}

/* Ask the engine whether it was built for this disc (`madeline --check`: 0 yes, 3 no);
 * with a kit, the kit, which also knows whether this version of the app built it. */
static void check_engine(int then_build) {
    g_then_build = then_build;
    if (!disc_valid()) { g_engine_ok = -1; update(); return; }
    if (g_kit[0]) {
        wchar_t args[3 * MAX_PATH];
        Job *job = new_job(WM_CHECKED);
        wchar_t a[2 * MAX_PATH];
        swprintf(args, 3 * MAX_PATH, L"check %ls \"%ls\\engine\"", arg(g_disc, a), g_data);
        kit_cmd(job, args);
        g_engine_ok = -1;
        update();
        start_job(job);
        return;
    }
    if (!file_exists(g_engine)) {
        g_engine_ok = 0;
        update();
        if (then_build) build_engine();
        return;
    }
    Job *job = new_job(WM_CHECKED);
    wchar_t a[2 * MAX_PATH];
    swprintf(job->cmd, 4096, L"\"%ls\" --check %ls", g_engine, arg(g_disc, a));
    g_engine_ok = -1;
    update();
    start_job(job);
}

static void play(void) {
    if (!can_play()) return;
    Job *job = new_job(WM_PLAYED);
    wchar_t a[2 * MAX_PATH];
    swprintf(job->cmd, 4096, L"\"%ls\" %ls", g_engine, arg(g_disc, a));
    lstrcpynW(job->cwd, g_disc, MAX_PATH);
    swprintf(job->log, MAX_PATH, L"%ls\\last-run.log", g_data);       /* what the engine printed, for when something is wrong */
    g_playing = 1;
    say(L"Playing\u2026");
    update();
    start_job(job);
}

static void use_folder(const wchar_t *path) {
    wchar_t was[MAX_PATH];
    size_t n;
    lstrcpynW(was, g_disc, MAX_PATH);
    lstrcpynW(g_disc, path, MAX_PATH);
    for (n = wcslen(g_disc); n > 3 && g_disc[n - 1] == L'\\'; ) g_disc[--n] = 0;
    if (disc_valid()) {
        say(L"Disc folder set.");
        save_settings();
        check_engine(1);
    } else {
        lstrcpynW(g_disc, was, MAX_PATH);
        say(L"That folder is not the game's disc: there is no MADEURO.EXE in it.");
        update();
    }
}

/* Unpack the player's own disc image. Windows reads disc images itself: mount
 * it, copy the files into the data folder, take it out again. (The paths go
 * by the environment, so nothing in them has to be quoted.) */
static void use_image(const wchar_t *iso) {
    wchar_t dest[MAX_PATH];
    Job *job = new_job(WM_UNPACKED);
    swprintf(dest, MAX_PATH, L"%ls\\disc", g_data);
    if (g_kit[0]) {                    /* the kit reads .iso (and the .zip it is downloaded in) itself */
        wchar_t args[3 * MAX_PATH];
        swprintf(args, 3 * MAX_PATH, L"extract --replace \"%ls\" \"%ls\"", iso, dest);
        kit_cmd(job, args);
        SHCreateDirectoryExW(NULL, g_data, NULL);
        g_busy = 1;
        say(L"Unpacking the disc image\u2026");
        update();
        start_job(job);
        return;
    }
    SHCreateDirectoryExW(NULL, dest, NULL);
    SetEnvironmentVariableW(L"MADELINE_ISO", iso);
    SetEnvironmentVariableW(L"MADELINE_DEST", dest);
    lstrcpyW(job->cmd, L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \""
        L"$ErrorActionPreference='Stop'; $i = Mount-DiskImage -ImagePath $env:MADELINE_ISO -PassThru; "
        L"try { $d = ($i | Get-Volume).DriveLetter; if (-not $d) { exit 2 }; "
        L"robocopy ([string]$d + ':\\') $env:MADELINE_DEST /MIR /A-:RSH /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null; "
        L"$rc = $LASTEXITCODE; attrib -R -S -H $env:MADELINE_DEST | Out-Null; if ($rc -ge 8) { exit 3 } } "
        L"finally { Dismount-DiskImage -ImagePath $env:MADELINE_ISO | Out-Null }; exit 0\"");
    g_busy = 1;
    say(L"Unpacking the disc image\u2026");
    update();
    start_job(job);
}

static void choose_image(void) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW of = { .lStructSize = sizeof of, .hwndOwner = g_wnd, .lpstrFile = file, .nMaxFile = MAX_PATH,
        .lpstrFilter = g_kit[0] ? L"Disc images (*.iso, *.zip)\0*.iso;*.zip\0All files\0*.*\0" : L"Disc images (*.iso)\0*.iso\0All files\0*.*\0", .lpstrTitle = L"Choose your own disc image of the game",
        .Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR };
    if (GetOpenFileNameW(&of)) use_image(file);
}
static void choose_folder(void) {
    wchar_t path[MAX_PATH];
    BROWSEINFOW bi = { .hwndOwner = g_wnd, .lpszTitle = L"Choose the folder that holds the disc's files (it contains MADEURO.EXE).",
        .ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON };
    PIDLIST_ABSOLUTE id = SHBrowseForFolderW(&bi);
    if (!id) return;
    if (SHGetPathFromIDListW(id, path)) use_folder(path);
    CoTaskMemFree(id);
}

/* ---- drawing ------------------------------------------------------------ */

static void gradient(HDC dc, int x0, int y0, int x1, int y1, COLORREF top, COLORREF bottom) {
    TRIVERTEX v[2] = {
        { x0, y0, GetRValue(top) << 8, GetGValue(top) << 8, GetBValue(top) << 8, 0 },
        { x1, y1, GetRValue(bottom) << 8, GetGValue(bottom) << 8, GetBValue(bottom) << 8, 0 } };
    GRADIENT_RECT r = { 0, 1 };
    GradientFill(dc, v, 2, &r, 1, GRADIENT_FILL_RECT_V);
}

/* A straw boater seen from the side: brim, crown, and a dark ribbon with two
 * tails. GDI has no smooth edges, so it is drawn four times the size and
 * shrunk. */
static void draw_hat(HDC dc, int x, int y, int w, int h, COLORREF top, COLORREF bottom) {
    int W = w * 4, H = h * 4;
    HDC m = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, W, H), old = SelectObject(m, bmp);
    HPEN ribbon = CreatePen(PS_SOLID, W * 55 / 1000, INK), line = CreatePen(PS_SOLID, W * 30 / 1000, INK);
    HBRUSH straw = CreateSolidBrush(HAT), ink = CreateSolidBrush(INK);
    gradient(m, 0, 0, W, H, top, bottom);
    SelectObject(m, ribbon);
    MoveToEx(m, W * 70 / 100, H * 50 / 100, NULL); LineTo(m, W * 93 / 100, H * 80 / 100);
    MoveToEx(m, W * 66 / 100, H * 52 / 100, NULL); LineTo(m, W * 80 / 100, H * 90 / 100);
    SelectObject(m, line); SelectObject(m, straw);
    Ellipse(m, W * 4 / 100, H * 46 / 100, W * 96 / 100, H * 70 / 100);
    RoundRect(m, W * 27 / 100, H * 20 / 100, W * 73 / 100, H * 58 / 100, W * 20 / 100, W * 20 / 100);
    SelectObject(m, ink);
    Rectangle(m, W * 27 / 100, H * 44 / 100, W * 73 / 100, H * 53 / 100);
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, NULL);
    StretchBlt(dc, x, y, w, h, m, 0, 0, W, H, SRCCOPY);
    SelectObject(m, old);
    DeleteObject(bmp); DeleteObject(ribbon); DeleteObject(line); DeleteObject(straw); DeleteObject(ink);
    DeleteDC(m);
}

/* The window's icon: the hat on a square of the header's paper. */
static HICON hat_icon(int size) {
    HDC screen = GetDC(NULL), m = CreateCompatibleDC(screen);
    HBITMAP colour = CreateCompatibleBitmap(screen, size, size), mask = CreateBitmap(size, size, 1, 1, NULL);
    HBITMAP old = SelectObject(m, colour);
    ICONINFO info = { TRUE, 0, 0, mask, colour };
    HICON icon;
    draw_hat(m, 0, 0, size, size, PAPER_T, PAPER_B);
    SelectObject(m, mask);
    PatBlt(m, 0, 0, size, size, BLACKNESS);
    SelectObject(m, old);
    icon = CreateIconIndirect(&info);
    DeleteObject(colour); DeleteObject(mask);
    DeleteDC(m);
    ReleaseDC(NULL, screen);
    return icon;
}

static void text_at(HDC dc, HFONT font, COLORREF colour, int x, int y, const wchar_t *text) {
    SelectObject(dc, font);
    SetTextColor(dc, colour);
    TextOutW(dc, x, y, text, lstrlenW(text));
}

/* The header is drawn once, into a picture, and the window shows the picture:
 * themed controls ask their parent to paint into their own DCs (WM_PRINTCLIENT),
 * so painting here must be quick and leave the DC as it was. */
static HBITMAP g_header;
static void draw_header(HDC dc, int width);
static void make_header(void) {
    RECT rc;
    GetClientRect(g_wnd, &rc);
    HDC wdc = GetDC(g_wnd), m = CreateCompatibleDC(wdc);
    g_header = CreateCompatibleBitmap(wdc, rc.right, S(92));
    HBITMAP old = SelectObject(m, g_header);
    draw_header(m, rc.right);
    GdiFlush();
    SelectObject(m, old);
    DeleteDC(m);
    ReleaseDC(g_wnd, wdc);
}
static void paint(HDC dc) {
    RECT rc;
    int state = SaveDC(dc);
    HDC m = CreateCompatibleDC(dc);
    HBITMAP old = SelectObject(m, g_header);
    HPEN rule = CreatePen(PS_SOLID, 1, RGB(222, 222, 222));
    GetClientRect(g_wnd, &rc);
    BitBlt(dc, 0, 0, rc.right, S(92), m, 0, 0, SRCCOPY);
    SelectObject(m, old);
    DeleteDC(m);
    SelectObject(dc, rule);
    MoveToEx(dc, 0, S(92), NULL); LineTo(dc, rc.right, S(92));
    MoveToEx(dc, 0, S(FOOT_Y), NULL); LineTo(dc, rc.right, S(FOOT_Y));
    RestoreDC(dc, state);
    DeleteObject(rule);
}
static void draw_header(HDC dc, int width) {
    gradient(dc, 0, 0, width, S(92), PAPER_T, PAPER_B);
    /* the hat sits on the gradient: give it the two colours the gradient has where it stands */
    draw_hat(dc, S(24), S(11), S(84), S(70), RGB(255, 232, 142), RGB(255, 243, 196));
    SetBkMode(dc, TRANSPARENT);
    text_at(dc, g_title, INK, S(124), S(10), L"The Little Girl in the Yellow Hat");
    text_at(dc, g_subtitle, COAT, S(124), S(40), L"Across Europe");
    text_at(dc, g_font, RGB(80, 84, 100), S(125), S(66), L"A 1996 adventure, running natively. Bring your own disc.");
}

static void draw_play(const DRAWITEMSTRUCT *d) {
    RECT r = d->rcItem;
    wchar_t text[32];
    int off = d->itemState & ODS_DISABLED, down = d->itemState & ODS_SELECTED;
    HBRUSH fill = CreateSolidBrush(off ? RGB(236, 236, 236) : down ? RGB(232, 184, 20) : HAT);
    HPEN edge = CreatePen(PS_SOLID, S(1), off ? RGB(200, 200, 200) : INK);
    HGDIOBJ b = SelectObject(d->hDC, fill), p = SelectObject(d->hDC, edge);
    FillRect(d->hDC, &r, g_white);
    RoundRect(d->hDC, r.left, r.top, r.right, r.bottom, S(10), S(10));
    GetWindowTextW(d->hwndItem, text, 32);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, off ? RGB(150, 150, 150) : INK);
    SelectObject(d->hDC, g_playfont);
    DrawTextW(d->hDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (d->itemState & ODS_FOCUS && !(d->itemState & ODS_NOFOCUSRECT)) { InflateRect(&r, -S(4), -S(4)); DrawFocusRect(d->hDC, &r); }
    SelectObject(d->hDC, b); SelectObject(d->hDC, p);
    DeleteObject(fill); DeleteObject(edge);
}

/* A picture of the window's inside, as a 24-bit BMP. */
static void snapshot(const wchar_t *file) {
    RECT rc;
    GetClientRect(g_wnd, &rc);
    int w = rc.right, h = rc.bottom, stride = (w * 3 + 3) & ~3;
    HDC wdc = GetDC(g_wnd), m = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, w, h), old = SelectObject(m, bmp);
    BITMAPINFO bi = { .bmiHeader = { .biSize = sizeof(BITMAPINFOHEADER), .biWidth = w, .biHeight = h, .biPlanes = 1, .biBitCount = 24, .biCompression = BI_RGB } };
    BITMAPFILEHEADER fh = { 0x4D42, sizeof fh + sizeof bi.bmiHeader + stride * h, 0, 0, sizeof fh + sizeof bi.bmiHeader };
    unsigned char *px = malloc((size_t)stride * h);
    FILE *f;
    PrintWindow(g_wnd, m, PW_CLIENTONLY | 2 /* PW_RENDERFULLCONTENT */);
    SelectObject(m, old);
    GetDIBits(m, bmp, 0, h, px, &bi, DIB_RGB_COLORS);
    if ((f = _wfopen(file, L"wb"))) {
        fwrite(&fh, sizeof fh, 1, f); fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f); fwrite(px, 1, (size_t)stride * h, f);
        fclose(f);
    }
    free(px);
    DeleteObject(bmp); DeleteDC(m); ReleaseDC(g_wnd, wdc);
}

/* ---- the window --------------------------------------------------------- */

static HWND control(const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id, HFONT font) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), g_wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)font, 0);
    return c;
}
static HWND label(const wchar_t *text, int x, int y, int w, int h, int id, HFONT font) { return control(L"STATIC", text, SS_NOPREFIX, x, y, w, h, id, font); }
static HWND checkbox(const wchar_t *text, int x, int y, int w, int id) { return control(L"BUTTON", text, BS_AUTOCHECKBOX | WS_TABSTOP, x, y, w, 22, id, g_font); }
static HWND dropdown(int x, int y, int w, int id) { return control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x, y, w, 200, id, g_font); }

/* Two columns, so the window is wide rather than tall and fits a small screen:
 * the disc and the saves on the left, how the game starts and looks on the right. */
static void create(void) {
    const int L = 24, LB = 340, R = 464, RD = 696, RW = 392;   /* left column (button at LB), right column (drop-downs at RD) */
    label(L"Game disc", L, 106, 280, 18, ID_TEXT, g_bold);
    g_disc_status = label(L"", L, 128, 306, 18, ID_DISC_STATUS, g_font);
    g_disc_path = control(L"STATIC", L"", SS_NOPREFIX | SS_PATHELLIPSIS, L, 147, 306, 18, ID_HINT, g_font);
    g_choose = control(L"BUTTON", L"Choose\u2026", BS_PUSHBUTTON | WS_TABSTOP, LB, 128, 100, 28, ID_CHOOSE, g_font);
    g_engine_status = label(L"", L, 172, 306, 18, ID_ENGINE_STATUS, g_font);
    g_engine_hint = label(L"The game's program is translated to native code from your disc, on this PC.", L, 191, 306, 34, ID_HINT, g_font);
    g_build = control(L"BUTTON", L"Build", BS_PUSHBUTTON | WS_TABSTOP, LB, 172, 100, 28, ID_BUILD, g_font);

    label(L"Saved games", L, 242, 280, 18, ID_TEXT, g_bold);
    g_open = control(L"BUTTON", L"Open folder", BS_PUSHBUTTON | WS_TABSTOP, LB, 236, 100, 26, ID_OPEN, g_font);
    g_saves = control(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_NOSORTHEADER | WS_BORDER | WS_TABSTOP, L, 268, LB + 100 - L, 150, ID_SAVES, g_font);
    SendMessageW(g_saves, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW col = { .mask = LVCF_TEXT | LVCF_WIDTH, .cx = S(160), .pszText = L"Name" };
    SendMessageW(g_saves, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
    col.cx = S(234); col.pszText = L"Saved";
    SendMessageW(g_saves, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);
    label(L"Each name signed into the book has its own game: sign in with it again and choose \u201Cold game\u201D.", L, 426, LB + 100 - L, 34, ID_HINT, g_font);

    label(L"Starting up", R, 106, 280, 18, ID_TEXT, g_bold);
    g_skip = checkbox(L"Skip the opening movies", R, 128, RW, ID_SKIP);
    g_name = checkbox(L"Fill in my name on the sign-in book", R, 154, RW, ID_NAME);
    g_full = checkbox(L"Start in full screen", R, 180, RW, ID_FULL);
    g_pad = checkbox(L"Play with a game controller", R, 206, RW, ID_PAD);
    g_ptr = checkbox(L"Use the game's own mouse pointers", R, 232, RW, ID_PTR);

    label(L"Picture", R, 272, 280, 18, ID_TEXT, g_bold);
    g_smooth = checkbox(L"Smooth the painting", R, 294, RW, ID_SMOOTH);
    g_xbr = checkbox(L"Redraw edges at twice the size (xBR)", R, 320, RW, ID_XBR);
    label(L"Scaling", R, 351, 200, 18, ID_TEXT, g_font);
    g_scale = dropdown(RD, 347, 160, ID_SCALE);
    label(L"Beside the picture", R, 381, 200, 18, ID_TEXT, g_font);
    g_bars = dropdown(RD, 377, 160, ID_BARS);
    for (int i = 0; i < 5; i++) SendMessageW(g_scale, CB_ADDSTRING, 0, (LPARAM)SCALE_LABELS[i]);
    SendMessageW(g_bars, CB_ADDSTRING, 0, (LPARAM)L"Soft blur");
    SendMessageW(g_bars, CB_ADDSTRING, 0, (LPARAM)L"Black");
    label(L"Smoothing melts the 256-colour speckle into washes of paint; xBR takes the stair-steps out of slanted lines.", R, 414, RW, 34, ID_HINT, g_font);

    g_status = label(L"", L, FOOT_Y + 14, 690, 34, ID_HINT, g_font);
    g_play = control(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, 736, FOOT_Y + 12, 120, 36, ID_PLAY, g_playfont);
}

static LRESULT CALLBACK wndproc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_PLAY: play(); return 0;
        case ID_CHOOSE: {
            RECT r;
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, IDM_IMAGE, L"Disc image (.iso)\u2026");
            AppendMenuW(menu, MF_STRING, IDM_FOLDER, L"Folder with the disc's files\u2026");
            GetWindowRect(g_choose, &r);
            TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_TOPALIGN, r.right, r.bottom, 0, wnd, NULL);
            DestroyMenu(menu);
            return 0;
        }
        case IDM_IMAGE: choose_image(); return 0;
        case IDM_FOLDER: choose_folder(); return 0;
        case ID_BUILD: build_engine(); return 0;
        case ID_OPEN: {
            wchar_t dir[MAX_PATH + 32];
            swprintf(dir, MAX_PATH + 32, L"%ls\\CWONDERS\\MADEURO", g_data);
            SHCreateDirectoryExW(NULL, dir, NULL);
            ShellExecuteW(wnd, L"open", dir, NULL, NULL, SW_SHOWNORMAL);
            return 0;
        }
        case ID_SKIP: case ID_NAME: case ID_FULL: case ID_SMOOTH: case ID_XBR: case ID_PAD: case ID_PTR:
            if (HIWORD(wp) == BN_CLICKED) { save_settings(); update(); }
            return 0;
        case ID_SCALE: case ID_BARS:
            if (HIWORD(wp) == CBN_SELCHANGE) { save_settings(); update(); }
            return 0;
        }
        break;
    case WM_UNPACKED: {
        wchar_t dest[MAX_PATH];
        g_busy = 0;
        swprintf(dest, MAX_PATH, L"%ls\\disc", g_data);
        if (wp != 0) {
            say(g_kit[0] && wp == 65 ? L"That image is not the game's disc: there is no MADEURO.EXE on it."
                : g_kit[0] ? L"Could not unpack that disc image. Is it a .iso, or a .zip with one inside?" : L"Could not read that disc image.");
            update();
            return 0;
        }
        use_folder(dest);
        if (disc_valid() && !wcscmp(g_disc, dest)) { if (!g_building) say(L"Disc unpacked."); }
        else say(L"That image is not the game's disc: there is no MADEURO.EXE on it.");
        update();
        return 0;
    }
    case WM_CHECKED:
        g_engine_ok = wp == 0;
        update();
        if (!g_engine_ok && g_then_build) build_engine();
        g_then_build = 0;
        return 0;
    case WM_BUILT:
        g_building = 0;
        if (wp == 0) { say(L"Engine built for this disc."); check_engine(0); }
        else {
            g_engine_ok = 0;
            wchar_t log[MAX_PATH];
            build_log(log);
            swprintf(g_message, 512, L"The build failed. See %ls", log);
            update();
        }
        return 0;
    case WM_PLAYED:
        g_playing = 0;
        load_settings(0);
        load_saves();
        if (wp == 0) say(L"");
        else swprintf(g_message, 512, L"The game stopped unexpectedly (code %d). See %ls\\last-run.log", (int)wp, g_data);
        update();
        return 0;
    case WM_TIMER:
        if (wp == TIMER_SNAPSHOT && !g_busy && !g_building && !(disc_valid() && g_engine_ok < 0)) {
            KillTimer(wnd, TIMER_SNAPSHOT);
            RedrawWindow(wnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            GdiFlush();
            snapshot(g_snapshot);
            DestroyWindow(wnd);
        }
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        int id = GetDlgCtrlID((HWND)lp);
        SetBkColor(dc, RGB(255, 255, 255));
        SetTextColor(dc, id == ID_HINT ? GREY
                       : id == ID_DISC_STATUS ? (disc_valid() && !g_busy ? GREEN : RGB(0, 0, 0))
                       : id == ID_ENGINE_STATUS ? (g_building ? RGB(0, 0, 0) : g_engine_ok == 1 ? GREEN : g_engine_ok == 0 ? ORANGE : GREY)
                       : RGB(0, 0, 0));
        return (LRESULT)g_white;
    }
    case WM_DRAWITEM:
        if (wp == ID_PLAY) { draw_play((const DRAWITEMSTRUCT *)lp); return TRUE; }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        paint(BeginPaint(wnd, &ps));
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT: paint((HDC)wp); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

static HFONT font(const wchar_t *face, int px, int weight, int italic) {
    return CreateFontW(-S(px), 0, 0, 0, weight, italic, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
}

/* One line of a small text file beside this program (written by `make app`). */
static void read_note(const wchar_t *dir, const wchar_t *name, wchar_t *out) {
    wchar_t path[MAX_PATH + 32];
    char line[MAX_PATH * 2] = "";
    FILE *f;
    swprintf(path, MAX_PATH + 32, L"%ls\\%ls", dir, name);
    out[0] = 0;
    if (!(f = _wfopen(path, L"rb"))) return;
    if (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        MultiByteToWideChar(CP_UTF8, 0, line, -1, out, MAX_PATH);
        for (wchar_t *p = out; *p; p++) if (*p == L'/') *p = L'\\';
    }
    fclose(f);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show) {
    wchar_t here[MAX_PATH], given[MAX_PATH] = L"", makefile[MAX_PATH + 16], kit_json[MAX_PATH + 16];
    (void)prev; (void)cmdline;            /* the command line is read whole, below */
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++) {
        if (!wcscmp(argv[i], L"--snapshot") && i + 1 < argc) lstrcpynW(g_snapshot, argv[++i], MAX_PATH);
        else if (argv[i][0] != L'-') GetFullPathNameW(argv[i], MAX_PATH, given, NULL);
    }

    /* Where things are: the data folder is the engine's (src/platform.c). */
    if (!GetEnvironmentVariableW(L"MADELINE_SAVE_DIR", g_data, MAX_PATH) || !g_data[0]) {
        wchar_t appdata[MAX_PATH] = L".";
        GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
        swprintf(g_data, MAX_PATH, L"%ls\\MadelineEuropeanAdventures", appdata);
    }
    for (wchar_t *p = g_data; *p; p++) if (*p == L'/') *p = L'\\';
    swprintf(g_ini, MAX_PATH, L"%ls\\madeline.ini", g_data);
    GetModuleFileNameW(NULL, here, MAX_PATH);
    *wcsrchr(here, L'\\') = 0;
    if (!GetEnvironmentVariableW(L"MADELINE_ENGINE", g_engine, MAX_PATH) || !g_engine[0])
        swprintf(g_engine, MAX_PATH, L"%ls\\madeline.exe", here);
    read_note(here, L"source-root.txt", g_root);
    read_note(here, L"msys-root.txt", g_msys);
    swprintf(makefile, MAX_PATH + 16, L"%ls\\Makefile", g_root);
    if (!file_exists(makefile)) g_root[0] = 0;
    if (!GetEnvironmentVariableW(L"MADELINE_KIT", g_kit, MAX_PATH) || !g_kit[0]) swprintf(g_kit, MAX_PATH, L"%ls\\kit", here);
    swprintf(kit_json, MAX_PATH + 16, L"%ls\\kit.json", g_kit);
    if (file_exists(kit_json)) {        /* a release: the engine lives in the data folder */
        wchar_t engine[MAX_PATH];
        if (!GetEnvironmentVariableW(L"MADELINE_ENGINE", engine, MAX_PATH) || !engine[0])
            swprintf(g_engine, MAX_PATH, L"%ls\\engine\\madeline.exe", g_data);
    } else g_kit[0] = 0;

    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);     /* the folder chooser needs it */
    HDC screen = GetDC(NULL);
    g_dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(NULL, screen);
    g_font = font(L"Segoe UI", 12, FW_NORMAL, 0);
    g_bold = font(L"Segoe UI", 13, FW_SEMIBOLD, 0);
    g_title = font(L"Georgia", 23, FW_BOLD, 0);
    g_subtitle = font(L"Georgia", 17, FW_BOLD, 1);
    g_playfont = font(L"Segoe UI", 15, FW_SEMIBOLD, 0);
    g_white = CreateSolidBrush(RGB(255, 255, 255));

    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = wndproc, .hInstance = inst, .hCursor = LoadCursorW(NULL, IDC_ARROW),
        .hbrBackground = g_white, .lpszClassName = L"YellowHatLauncher",
        .hIcon = hat_icon(GetSystemMetrics(SM_CXICON)), .hIconSm = hat_icon(GetSystemMetrics(SM_CXSMICON)) };
    RegisterClassExW(&wc);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT r = { 0, 0, S(880), S(FOOT_Y + 62) }, work;
    AdjustWindowRectEx(&r, style, FALSE, 0);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    g_wnd = CreateWindowExW(0, wc.lpszClassName, TITLE, style,
        work.left + (work.right - work.left - (r.right - r.left)) / 2, work.top + (work.bottom - work.top - (r.bottom - r.top)) / 2,
        r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    make_header();
    create();
    load_settings(1);
    load_saves();
    update();
    if (given[0]) {                                     /* a disc dropped on the program, or named on its command line */
        if (GetFileAttributesW(given) & FILE_ATTRIBUTE_DIRECTORY) use_folder(given); else use_image(given);
    } else check_engine(0);
    ShowWindow(g_wnd, g_snapshot[0] ? SW_SHOWNOACTIVATE : show);
    UpdateWindow(g_wnd);
    SetFocus(g_play);
    if (g_snapshot[0]) SetTimer(g_wnd, TIMER_SNAPSHOT, 700, NULL);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (IsDialogMessageW(g_wnd, &m)) continue;      /* Tab between the controls, Enter for Play */
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
