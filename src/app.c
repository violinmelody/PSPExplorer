#include "app.h"
#include "explorer.h"
#include "image.h"
#include "audio.h"
#include "renderer.h"
#include "settings.h"
#include <pspctrl.h>
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef enum{
	MODE_FILES, MODE_MENU, MODE_HUE, MODE_ABOUT, MODE_EDITOR, MODE_IMAGE,
	MODE_PROMPT, MODE_DELETE_CONFIRM, MODE_AUDIO
	} Mode;

typedef enum{
	KEY_CHAR, KEY_CAPS, KEY_BACKSPACE, KEY_SPACE, KEY_ENTER, KEY_CANCEL, KEY_OK
	} KeyKind;

typedef struct{
	KeyKind kind;
	char ch;
	} KeyAction;

static volatile int running = 1;
static const char *keyboard_rows[] = {
	"qwertyuiop", "asdfghjkl", "zxcvbnm", "1234567890", "@#$_&-+()/", "*\"':;!?.,"
};
static const int keyboard_widths[] = {10, 9, 7, 10, 10, 9, 6};
#define KEYBOARD_ROWS 7

static int exit_callback(int a, int b, void *c){
	(void)a;
	(void)b;
	(void)c;
	running = 0;
	return 0;
}

static int callback_thread(SceSize a, void *b){
	int id;
	(void)a;
	(void)b;
	id=sceKernelCreateCallback("Exit",exit_callback,NULL);
	sceKernelRegisterExitCallback(id);
	sceKernelSleepThreadCB();
	return 0;
}

static void setup_callbacks(void){
	int id=sceKernelCreateThread("callbacks",callback_thread,0x11,0xFA0,0,NULL);
	if(id>=0) sceKernelStartThread(id,0,NULL);
}

static int has_extension(const char *n,const char *e){
	const char *d=strrchr(n,'.');
	return d&&!strcasecmp(d,e);
}

static int is_image_file(const char *n){
	return has_extension(n,".png")||has_extension(n,".jpg")||has_extension(n,".jpeg")||has_extension(n,".bmp");
}

static int is_audio_file(const char *n){
	return audio_is_supported(n);
}

static int is_text_file(const char *n){
	return has_extension(n,".txt")||has_extension(n,".ini")||has_extension(n,".cfg")||has_extension(n,".log")||has_extension(n,".md")||has_extension(n,".c")||has_extension(n,".h")||has_extension(n,".json")||has_extension(n,".xml");
}

static void selected_path(const Pane *p,char *out){
	if(p->count) path_join(out,PATH_MAX_PSP,p->path,p->entries[p->selected].name);
	else out[0]=0;
}

static void keep_visible(Pane *p){
	const int rows=14;
	if(p->selected<p->scroll) p->scroll=p->selected;
	if(p->selected>=p->scroll+rows) p->scroll=p->selected-rows+1;
}

static void keyboard_move(int *row,int *col,int dr,int dc){
	int nr=*row+dr;
	if(nr<0) nr=KEYBOARD_ROWS-1;
	if(nr>=KEYBOARD_ROWS) nr=0;
	*row=nr;
	if(*col>=keyboard_widths[*row]) *col=keyboard_widths[*row]-1;
	if(dc){
		*col+=dc;
		if(*col<0) *col=keyboard_widths[*row]-1;
		if(*col>=keyboard_widths[*row]) *col=0;
	}
}

static KeyAction keyboard_action(int row,int col,int caps){
	KeyAction a={KEY_CHAR,0};
	if(row<6){
		a.ch=keyboard_rows[row][col];
		if(caps&&a.ch>='a'&&a.ch<='z') a.ch=(char)(a.ch-'a'+'A');
		return a;
	}
	switch(col){
		case 0:
			a.kind=KEY_CAPS;
			break;
		case 1:
			a.kind=KEY_BACKSPACE;
			break;
		case 2:
			a.kind=KEY_SPACE;
			break;
		case 3:
			a.kind=KEY_ENTER;
			break;
		case 4:
			a.kind=KEY_CANCEL;
			break;
		default:
			a.kind=KEY_OK;
			break;
	}
	return a;
}

static char *load_text(const char *path,int *len){
	FILE *f=fopen(path,"rb");
	char *b;
	if(!f) return NULL;
	b=calloc(1,32768);
	if(!b){
		fclose(f);
		return NULL;
	}
	*len=(int)fread(b,1,32767,f);
	b[*len]=0;
	fclose(f);
	return b;
}

static int save_text(const char *path,const char *b,int len){
	FILE *f=fopen(path,"wb");
	if(!f) return -1;
	if(fwrite(b,1,(size_t)len,f)!=(size_t)len){
		fclose(f);
		return -1;
	}
	fclose(f);
	return 0;
}

static void insert_char(char *b,int *len,int *cursor,char ch){
	if(*len>=32766) return;
	memmove(b+*cursor+1,b+*cursor,(size_t)(*len-*cursor+1));
	b[*cursor]=ch;
	(*cursor)++;
	(*len)++;
}

static void backspace_char(char *b,int *len,int *cursor){
	if(*cursor<=0) return;
	memmove(b+*cursor-1,b+*cursor,(size_t)(*len-*cursor+1));
	(*cursor)--;
	(*len)--;
}

static int folder_char_allowed(const char *value,char ch){
	if((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')) return 1;
	if(ch=='.' && strchr(value,'.')==NULL) return 1;
	return 0;
}

int app_run(void){
	Pane panes[2];
	Settings settings;
	Clipboard clipboard={0};
	SceCtrlData pad={0},old={0};
	Mode mode=MODE_FILES;
	Image image={0};
	AudioPlayer audio={0};
	int audio_button=1;
	
	const char *menus[]={"COPY","CUT / MOVE","PASTE","RENAME","DELETE","NEW FILE","NEW FOLDER","TEXT EDITOR","APPEARANCE","ABOUT","QUIT"};
	int active=0,menu=0,prompt_operation=0,confirm_selected=0,key_row=0,key_col=0,caps=0,prompt_is_folder=0;
	float image_zoom=1.0f;
	int image_pan_x=0,image_pan_y=0;
	
	char status[80]="",path[PATH_MAX_PSP]="",editor_path[PATH_MAX_PSP]="",prompt[NAME_MAX_PSP]="";
	char *edit=NULL;
	int edit_len=0,edit_cursor=0;
	
	setup_callbacks();
	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
	settings_load(&settings);
	pane_init(&panes[0],"ms0:/");
	pane_init(&panes[1],"ms0:/");
	renderer_init();
	
	while(running){
		unsigned int press;
		sceCtrlPeekBufferPositive(&pad,1);
		press=pad.Buttons&~old.Buttons;
		if(mode==MODE_FILES){
			Pane *p=&panes[active];
			if(press&PSP_CTRL_LTRIGGER) active=0;
			if(press&PSP_CTRL_RTRIGGER) active=1;
			
			p=&panes[active];
			if((press&PSP_CTRL_UP)&&p->selected>0){
				p->selected--;
			}
			if((press&PSP_CTRL_DOWN)&&p->selected<p->count-1){
				p->selected++;
			}
			
			keep_visible(p);
			if(press&PSP_CTRL_CIRCLE){
				pane_up(p);
			}
			if(press&PSP_CTRL_SQUARE){
				pane_toggle_mark(p);
				snprintf(status,sizeof(status),"%d SELECTED",pane_mark_count(p));
			}
			if(press&PSP_CTRL_TRIANGLE){
				mode=MODE_MENU;
				menu=0;
			}
			if((press&PSP_CTRL_CROSS)&&p->count){
				if(p->entries[p->selected].is_dir) pane_enter(p);
				else{
					selected_path(p,path);
					if(is_image_file(path)){
						if(image_load(path,&image)==0){
							image_zoom=1.0f;
							image_pan_x=image_pan_y=0;
							mode=MODE_IMAGE;
						}
						else snprintf(status,sizeof(status),"IMAGE LOAD FAILED");
					}
					else if(is_audio_file(path)){
						if(audio_player_open(&audio,path)==0){
							audio_button=1;
							mode=MODE_AUDIO;
						}
						else snprintf(status,sizeof(status),"AUDIO OPEN FAILED");
					}
					else if(is_text_file(path)){
						edit=load_text(path,&edit_len);
						if(edit){
							snprintf(editor_path,sizeof(editor_path),"%s",path);
							edit_cursor=0;
							key_row=key_col=0;
							caps=0;
							mode=MODE_EDITOR;
						}
						else snprintf(status,sizeof(status),"TEXT OPEN FAILED");
					}
				}
			}
		} else if(mode==MODE_MENU) {
			Pane *p=&panes[active];
			int marks=pane_mark_count(p);
			int count;
			count=marks?6:(int)(sizeof(menus)/sizeof(menus[0]));
			if(press&PSP_CTRL_UP) menu=(menu+count-1)%count;
			if(press&PSP_CTRL_DOWN) menu=(menu+1)%count;
			if(press&PSP_CTRL_CIRCLE){mode=MODE_FILES;menu=0;}
			if(press&PSP_CTRL_CROSS){
				selected_path(p,path);
				if(marks){
					if(menu==0||menu==1){
						int i;
						clipboard.count=0;
						clipboard.cut=(menu==1);
						for(i=0;i<p->count&&clipboard.count<CLIPBOARD_MAX;i++)
						if(p->marked[i]){
							path_join(clipboard.sources[clipboard.count],PATH_MAX_PSP,p->path,p->entries[i].name)
							;clipboard.count++;
						}
						snprintf(status,sizeof(status),"%d ITEM(S) READY",clipboard.count);
						mode=MODE_FILES;
					}
					else if(menu==2){
						confirm_selected=0;
						mode=MODE_DELETE_CONFIRM;
					}
					else if(menu==3){
						pane_mark_all(p);
						snprintf(status,sizeof(status),"%d SELECTED",pane_mark_count(p));
						mode=MODE_FILES;
					}
					else if(menu==4){
						pane_clear_marks(p);
						status[0]=0;
						mode=MODE_FILES;
					}
					else mode=MODE_FILES;
				}
				else if(menu==0&&path[0]){
					snprintf(clipboard.sources[0],PATH_MAX_PSP,"%s",path);
					clipboard.count=1;
					clipboard.cut=0;
					snprintf(status,sizeof(status),"COPIED");
					mode=MODE_FILES;
				}
				else if(menu==1&&path[0]){
					snprintf(clipboard.sources[0],PATH_MAX_PSP,"%s",path);
					clipboard.count=1;
					clipboard.cut=1;
					snprintf(status,sizeof(status),"MOVE READY");
					mode=MODE_FILES;
				}
				else if(menu==2&&clipboard.count>0){
					int i,ok=1;
					for(i=0;i<clipboard.count;i++){
						char dst[PATH_MAX_PSP];
						const char *base=strrchr(clipboard.sources[i],'/');
						path_join(dst,sizeof(dst),p->path,base?base+1:clipboard.sources[i]);
						if((clipboard.cut?fs_move:fs_copy)(clipboard.sources[i],dst)!=0) ok=0;
					}
					if(clipboard.cut&&ok) clipboard.count=0;
					snprintf(status,sizeof(status),ok?"DONE":"OPERATION FAILED");
					pane_refresh(&panes[0]);
					pane_refresh(&panes[1]);
					mode=MODE_FILES;
				}
				else if(menu==3||menu==5||menu==6){
					prompt_operation=menu;
					prompt[0]=0;
					prompt_is_folder=(menu==6)||(menu==3&&p->count&&p->entries[p->selected].is_dir);
					if(menu==3&&p->count) snprintf(prompt,sizeof(prompt),"%s",p->entries[p->selected].name);
					key_row=key_col=0;
					caps=0;
					mode=MODE_PROMPT;
				}
				else if(menu==4&&path[0]){
					confirm_selected=0;
					mode=MODE_DELETE_CONFIRM;
				}
				else if(menu==7){
					if(path[0]&&!p->entries[p->selected].is_dir){
						edit=load_text(path,&edit_len);
						if(edit){
							snprintf(editor_path,sizeof(editor_path),"%s",path);
							edit_cursor=0;
							key_row=key_col=0;
							caps=0;
							mode=MODE_EDITOR;
						}
						else snprintf(status,sizeof(status),"TEXT OPEN FAILED");
					}
					else snprintf(status,sizeof(status),"SELECT A FILE");
				}
				else if(menu==8) mode=MODE_HUE;
				else if(menu==9) mode=MODE_ABOUT;
				else if(menu==10) running=0;
			}
		}
		else if(mode==MODE_DELETE_CONFIRM){
			Pane *p=&panes[active];
			int marks=pane_mark_count(p);
			if(press&(PSP_CTRL_LEFT|PSP_CTRL_RIGHT)){
				confirm_selected^=1;
			}
			if(press&PSP_CTRL_CIRCLE){
				mode=MODE_MENU;
			}
			if(press&PSP_CTRL_CROSS){
				if(confirm_selected){
					int ok=1;
					if(marks){
						int i;
						for(i=0;i<p->count;i++) if(p->marked[i]){
							char q[PATH_MAX_PSP];
							path_join(q,sizeof(q),p->path,p->entries[i].name);
							if(fs_delete(q)!=0) ok=0;
						}
					}
					else if(path[0]&&fs_delete(path)!=0) ok=0;
					snprintf(status,sizeof(status),ok?"DELETED":"DELETE FAILED");
					pane_refresh(&panes[0]);
					pane_refresh(&panes[1]);
					mode=MODE_FILES;
				}
				else mode=MODE_MENU;
			}
		}
		else if(mode==MODE_HUE){
			if(pad.Buttons&PSP_CTRL_LEFT) settings.hue=(settings.hue+359)%360;
			else if(pad.Buttons&PSP_CTRL_RIGHT) settings.hue=(settings.hue+1)%360;
			
			if(press&PSP_CTRL_CIRCLE){
				settings_save(&settings);
				mode=MODE_MENU;
			}
		}
		else if(mode==MODE_ABOUT){
			if(press&(PSP_CTRL_CIRCLE|PSP_CTRL_CROSS)) mode=MODE_MENU;
		}
		else if(mode==MODE_IMAGE){
			if(press&PSP_CTRL_CIRCLE){
				image_free(&image);
				mode=MODE_FILES;
			}
			if(press&PSP_CTRL_LTRIGGER){
				image_zoom-=0.25f;
				if(image_zoom<0.25f) image_zoom=0.25f;
			}
			if(press&PSP_CTRL_RTRIGGER){
				image_zoom+=0.25f;
				if(image_zoom>8.0f) image_zoom=8.0f;
			}
			if(press&PSP_CTRL_SQUARE){
				image_zoom=1.0f;
				image_pan_x=image_pan_y=0;
			}
			if(pad.Buttons&PSP_CTRL_LEFT) image_pan_x-=3;
			if(pad.Buttons&PSP_CTRL_RIGHT) image_pan_x+=3;
			if(pad.Buttons&PSP_CTRL_UP) image_pan_y-=3;
			if(pad.Buttons&PSP_CTRL_DOWN) image_pan_y+=3;
		}
		else if(mode==MODE_AUDIO){
			if(press&PSP_CTRL_CIRCLE){
				audio_player_close(&audio);
				mode=MODE_FILES;
			}
			if((press&PSP_CTRL_LEFT)&&audio_button>0) audio_button--;
			if((press&PSP_CTRL_RIGHT)&&audio_button<2) audio_button++;
			if(press&PSP_CTRL_LTRIGGER) audio_player_seek(&audio,-5000);
			if(press&PSP_CTRL_RTRIGGER) audio_player_seek(&audio,5000);
			if(press&PSP_CTRL_CROSS){
				if(audio_button==0) audio_player_seek(&audio,-5000);
				else if(audio_button==1) audio_player_toggle(&audio);
				else audio_player_seek(&audio,5000);
			}
		}
		else if(mode==MODE_EDITOR){
			KeyAction a;
			if(press&PSP_CTRL_UP) keyboard_move(&key_row,&key_col,-1,0);
			if(press&PSP_CTRL_DOWN) keyboard_move(&key_row,&key_col,1,0);
			if(press&PSP_CTRL_LEFT) keyboard_move(&key_row,&key_col,0,-1);
			if(press&PSP_CTRL_RIGHT) keyboard_move(&key_row,&key_col,0,1);
			if((press&PSP_CTRL_LTRIGGER)&&edit_cursor>0) edit_cursor--;
			if((press&PSP_CTRL_RTRIGGER)&&edit_cursor<edit_len) edit_cursor++;
			if(press&PSP_CTRL_CROSS){
				a=keyboard_action(key_row,key_col,caps);
				if(a.kind==KEY_CHAR) insert_char(edit,&edit_len,&edit_cursor,a.ch);
				else if(a.kind==KEY_CAPS) caps=!caps;
				else if(a.kind==KEY_BACKSPACE) backspace_char(edit,&edit_len,&edit_cursor);
				else if(a.kind==KEY_SPACE) insert_char(edit,&edit_len,&edit_cursor,' ');
				else if(a.kind==KEY_ENTER) insert_char(edit,&edit_len,&edit_cursor,'\n');
				else if(a.kind==KEY_CANCEL){
					free(edit);
					edit=NULL;
					mode=MODE_FILES;
				}
				else if(a.kind==KEY_OK){
					snprintf(status,sizeof(status),save_text(editor_path,edit,edit_len)==0?"SAVED":"SAVE FAILED");
					free(edit);
					edit=NULL;
					mode=MODE_FILES;
				}
			}
		}
		else if(mode==MODE_PROMPT){
			KeyAction a;
			if(press&PSP_CTRL_UP) keyboard_move(&key_row,&key_col,-1,0);
			if(press&PSP_CTRL_DOWN) keyboard_move(&key_row,&key_col,1,0);
			if(press&PSP_CTRL_LEFT) keyboard_move(&key_row,&key_col,0,-1);
			if(press&PSP_CTRL_RIGHT) keyboard_move(&key_row,&key_col,0,1);
			if(press&PSP_CTRL_CROSS){
				a=keyboard_action(key_row,key_col,caps);
				if(a.kind==KEY_CHAR&&strlen(prompt)<NAME_MAX_PSP-2&&(!prompt_is_folder||folder_char_allowed(prompt,a.ch))){
					size_t l=strlen(prompt);
					prompt[l]=a.ch;
					prompt[l+1]=0;
				}
				else if(a.kind==KEY_CAPS) caps=!caps;
				else if(a.kind==KEY_BACKSPACE&&prompt[0]) prompt[strlen(prompt)-1]=0;
				else if(a.kind==KEY_SPACE&&!prompt_is_folder&&strlen(prompt)<NAME_MAX_PSP-2){
					size_t l=strlen(prompt);
					prompt[l]=' ';
					prompt[l+1]=0;
				}
				else if(a.kind==KEY_CANCEL) mode=MODE_MENU;
				else if(a.kind==KEY_OK){
					Pane *p=&panes[active];
					char dst[PATH_MAX_PSP];
					int result=-1;
					if(prompt[0]){
						path_join(dst,sizeof(dst),p->path,prompt);
						if(prompt_operation==3&&path[0]) result=fs_rename(path,dst);
						else if(prompt_operation==5) result=fs_touch(dst);
						else if(prompt_operation==6) result=fs_mkdir(dst);
						snprintf(status,sizeof(status),result==0?"DONE":"OPERATION FAILED");
						pane_refresh(&panes[0]);
						pane_refresh(&panes[1]);
					}
					mode=MODE_FILES;
				}
			}
		}
	
		renderer_begin(settings.hue);
		if(mode==MODE_FILES){
			renderer_header("PSPEXPLORER",active?"RIGHT":"LEFT");
			renderer_explorer(&panes[0],active==0,0,status);
			renderer_explorer(&panes[1],active==1,1,status);
		}
		else if(mode==MODE_MENU){
			int mc=pane_mark_count(&panes[active]);
			const char*mm[]={"COPY SELECTED","CUT / MOVE SELECTED","DELETE SELECTED","SELECT ALL","CLEAR SELECTION","BACK"};
			renderer_menu(mc?"SELECTED ITEMS":"FILE OPERATIONS",mc?mm:menus,mc?6:(int)(sizeof(menus)/sizeof(menus[0])),menu);
		}
		else if(mode==MODE_HUE) renderer_hue(settings.hue);
		else if(mode==MODE_ABOUT) renderer_about();
		else if(mode==MODE_IMAGE) renderer_image(path,image.pixels,image.w,image.h,image_zoom,image_pan_x,image_pan_y);
		else if(mode==MODE_AUDIO) renderer_audio(audio.filename,audio.artist,audio.title,audio.waveform,AUDIO_WAVEFORM_POINTS,audio.position_ms,audio.duration_ms,audio.playing,audio.cover.pixels,audio.cover.w,audio.cover.h,audio_button);
		else if(mode==MODE_EDITOR) renderer_text_editor(editor_path,edit,edit_cursor,key_row,key_col,caps);
		else if(mode==MODE_PROMPT) renderer_name_prompt(prompt_operation==3?"RENAME":prompt_operation==5?"NEW FILE":"NEW FOLDER",prompt,key_row,key_col,caps,prompt_is_folder);
		else if(mode==MODE_DELETE_CONFIRM){
			int mc=pane_mark_count(&panes[active]);
			const char*detail=mc?"MULTIPLE SELECTED ITEMS":(panes[active].count?panes[active].entries[panes[active].selected].name:"");
			renderer_confirm("DELETE",mc?"DELETE SELECTED ITEMS?":"DELETE THIS ITEM?",detail,confirm_selected);
		}
		renderer_end();
		old=pad;
	}
	
    if(edit){
		free(edit);
	}
	
    image_free(&image);
    audio_player_close(&audio);
    settings_save(&settings);
    renderer_shutdown();
    sceKernelExitGame();
    return 0;
}
