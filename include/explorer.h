#ifndef PSPEXPLORER_EXPLORER_H
#define PSPEXPLORER_EXPLORER_H
#include <stddef.h>
#define PATH_MAX_PSP 512
#define MAX_ENTRIES 256
#define NAME_MAX_PSP 128
#define CLIPBOARD_MAX MAX_ENTRIES

typedef struct{
	char name[NAME_MAX_PSP];
	int is_dir;
	unsigned long size;
	} FileEntry;

typedef struct{
	char path[PATH_MAX_PSP];
	FileEntry entries[MAX_ENTRIES];
	unsigned char marked[MAX_ENTRIES];
	int count, selected, scroll;
	} Pane;

typedef struct{
	char sources[CLIPBOARD_MAX][PATH_MAX_PSP];
	int count;
	int cut;
	} Clipboard;

void pane_init(Pane *p, const char *path);
int pane_refresh(Pane *p);
int pane_enter(Pane *p);
void pane_up(Pane *p);
void pane_toggle_mark(Pane *p);
void pane_clear_marks(Pane *p);
void pane_mark_all(Pane *p);
int pane_mark_count(const Pane *p);

int fs_copy(const char *src,const char *dst);
int fs_delete(const char *path);
int fs_move(const char *src,const char *dst);
int fs_mkdir(const char *path);
int fs_touch(const char *path);
int fs_rename(const char *src,const char *dst);

void path_join(char *out,size_t n,const char *a,const char *b);

#endif
