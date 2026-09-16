#include "explorer.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pspiofilemgr.h>

static int cmp(const void *a,const void *b){
	const FileEntry*x=a,*y=b;
	if(x->is_dir!=y->is_dir) return y->is_dir-x->is_dir;
	return strcasecmp(x->name,y->name);
}

void path_join(char*out,size_t n,const char*a,const char*b){
	snprintf(out,n,"%s%s%s",a,(a[0]&&a[strlen(a)-1]=='/')?"":"/",b);
}

void pane_init(Pane*p,const char*path){
	memset(p,0,sizeof(*p));
	snprintf(p->path,sizeof(p->path),"%s",path);
	pane_refresh(p);
}

int pane_refresh(Pane *p) {
    SceUID dir;
    SceIoDirent entry;
	
    if (!p) return -1;
    p->count = 0;
    memset(p->marked, 0, sizeof(p->marked));
	
    dir = sceIoDopen(p->path);
    if (dir < 0) return -1;
	
    memset(&entry, 0, sizeof(entry));
    while (p->count < MAX_ENTRIES && sceIoDread(dir, &entry) > 0) {
        FileEntry *out;
        size_t name_len;
        if (!strcmp(entry.d_name, ".") || !strcmp(entry.d_name, "..")) {
            memset(&entry, 0, sizeof(entry));
            continue;
        }
        out = &p->entries[p->count];
        name_len = strlen(entry.d_name);
        if (name_len >= sizeof(out->name)) name_len = sizeof(out->name) - 1;
        memcpy(out->name, entry.d_name, name_len);
        out->name[name_len] = '\0';
        out->is_dir = FIO_S_ISDIR(entry.d_stat.st_mode) ? 1 : 0;
        out->size = (unsigned long)entry.d_stat.st_size;
        p->count++;
        memset(&entry, 0, sizeof(entry));
    }
    sceIoDclose(dir);
	
    qsort(p->entries, p->count, sizeof(FileEntry), cmp);
    if (p->selected >= p->count) p->selected = p->count ? p->count - 1 : 0;
    if (p->scroll > p->selected) p->scroll = p->selected;
    return 0;
}
int pane_enter(Pane*p){
	char next[PATH_MAX_PSP];
	if(!p->count||!p->entries[p->selected].is_dir) return -1;
	path_join(next,sizeof(next),p->path,p->entries[p->selected].name);
	snprintf(p->path,sizeof(p->path),"%s",next);
	p->selected=p->scroll=0;
	return pane_refresh(p);
}

void pane_up(Pane*p){
	char*s;
	if(!strcmp(p->path,"ms0:/")||!strcmp(p->path,"ef0:/")) return;
	while(strlen(p->path)>4&&p->path[strlen(p->path)-1]=='/') p->path[strlen(p->path)-1]=0;s=strrchr(p->path,'/');
	if(s&&s>p->path+3) *s=0;
	else snprintf(p->path,sizeof(p->path),"ms0:/");
	p->selected=p->scroll=0;pane_refresh(p);
}

static int copy_file(const char*s,const char*d){
	FILE*i=fopen(s,"rb"),*o;char b[16384];
	size_t n;
	if(!i) return -1;
	o=fopen(d,"wb");
	if(!o){
		fclose(i);
		return -1;
	}
	
	while((n=fread(b,1,sizeof(b),i))>0) if(fwrite(b,1,n,o)!=n){
		fclose(i);
		fclose(o);
		return -1;
	}
	fclose(i);
	fclose(o);
	return 0;
}

int fs_copy(const char*s,const char*d){
	struct stat st;
	if(stat(s,&st)<0) return -1;
	if(S_ISDIR(st.st_mode)){
		DIR*dir;
		struct dirent*de;
		char a[PATH_MAX_PSP],b[PATH_MAX_PSP];
		mkdir(d,0777);
		dir=opendir(s);
		if(!dir) return -1;
		while((de=readdir(dir))){
			if(!strcmp(de->d_name,".")||!strcmp(de->d_name,"..")) continue;
			path_join(a,sizeof(a),s,de->d_name);
			path_join(b,sizeof(b),d,de->d_name);
			if(fs_copy(a,b)<0){
				closedir(dir);return-1;
			}
		}
		closedir(dir);
		return 0;
	}
	return copy_file(s,d);
}

int fs_delete(const char*p){
	struct stat st;
	if(stat(p,&st)<0) return -1;
	if(S_ISDIR(st.st_mode)){
		DIR*d=opendir(p);
		struct dirent*de;
		char q[PATH_MAX_PSP];
		if(!d) return -1;
		while((de=readdir(d))){
			if(!strcmp(de->d_name,".")||!strcmp(de->d_name,"..")) continue;
			path_join(q,sizeof(q),p,de->d_name);
			if(fs_delete(q)<0){
				closedir(d);
				return -1;
			}
		}
		closedir(d);
		return rmdir(p);
	}
	return remove(p);
}

int fs_move(const char*s,const char*d){
	if(rename(s,d)==0) return 0;
	if(fs_copy(s,d)<0) return -1;
	return fs_delete(s);
}

int fs_mkdir(const char*p){
	int r=sceIoMkdir(p,0777);
	if(r>=0) return 0;
	return mkdir(p,0777);
}

int fs_touch(const char*p){
	FILE*f=fopen(p,"wb");
	if(!f) return -1;
	fclose(f);
	return 0;
}

int fs_rename(const char*s,const char*d){
	return rename(s,d);
}

void pane_toggle_mark(Pane *p){
	if(p&&p->count>0) p->marked[p->selected]^=1;
}

void pane_clear_marks(Pane *p){
	if(p) memset(p->marked,0,sizeof(p->marked));
}

void pane_mark_all(Pane *p){
	int i;
	if(!p) return;
	for(i=0;i<p->count;i++) p->marked[i]=1;
}

int pane_mark_count(const Pane *p){
	int i,n=0;
	if(!p) return 0;
	for(i=0;i<p->count;i++) if(p->marked[i])n++;
	return n;
}
