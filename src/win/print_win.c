/*
 * print_win.c - a printed page on Windows: the PDF print.c made is opened in
 * whatever the player has for PDFs (a browser, if nothing else), which has
 * the print dialog. See ../madeline.h for the interface; it is here because
 * it needs <windows.h>.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdint.h>

int madeline_print_hand_over(const char *pdf, const char *title) {
    char path[MAX_PATH * 2];
    (void)title;
    snprintf(path, sizeof path, "%s", pdf);
    for (char *p = path; *p; p++) if (*p == '/') *p = '\\';
    return (intptr_t)ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL) > 32;   /* 32 and under are its errors */
}
