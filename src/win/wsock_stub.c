/*
 * wsock_stub.c - WSOCK32 with nothing in it. win32hle's wsock32.c is written
 * on BSD sockets, which Windows does not have under those names, and this
 * game makes no network calls: on Windows that file is left out of the build
 * and its table, which recomp_host_init registers, is this empty one.
 */
#include "win32hle.h"

const win32hle_shim win32hle_wsock32[] = {
    { 0, 0 }
};

/* the message pump asks for socket notifications on every turn: there are none */
void hle_ws_poll(void) {}
void (*hle_ws_broadcast_hook)(int fd, const void *buf, uint32_t len, unsigned port);
