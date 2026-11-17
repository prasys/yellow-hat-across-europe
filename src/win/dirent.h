/* <dirent.h> for Windows: see compat_win.h. MinGW has one, but its entries do
 * not say whether they are directories (d_type), which win32hle reads; this
 * one does, straight from FindFirstFile. */
#ifndef MADELINE_COMPAT_DIRENT_H
#define MADELINE_COMPAT_DIRENT_H

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

struct compat_dirent { unsigned char d_type; char d_name[260]; };
typedef struct compat_dir compat_dir;

compat_dir           *compat_opendir(const char *path);
struct compat_dirent *compat_readdir(compat_dir *d);
int                   compat_closedir(compat_dir *d);

#define dirent   compat_dirent
#define DIR      compat_dir
#define opendir  compat_opendir
#define readdir  compat_readdir
#define closedir compat_closedir

#endif
