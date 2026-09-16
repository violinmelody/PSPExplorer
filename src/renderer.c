#include "renderer.h"
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>
#define W 480
#define H 272
#define IMAGE_TEX_W 512
#define IMAGE_TEX_H 256
#define ABGR(a,b,g,r) ((unsigned int)(((a)<<24)|((b)<<16)|((g)<<8)|(r)))

static unsigned int __attribute__((aligned(16))) dl[262144];
static unsigned int __attribute__((aligned(16))) image_view_texture[IMAGE_TEX_W * IMAGE_TEX_H];
static unsigned int accent,bg,panel,soft;
typedef struct{
	unsigned int c;
	short x,y,z,p;
	}V;
typedef char stride[(sizeof(V)==12)?1:-1];
static const unsigned char font[][5]={['0']={62,81,73,69,62},['1']={0,66,127,64,0},['2']={98,81,73,73,70},['3']={34,65,73,73,54},['4']={24,20,18,127,16},['5']={39,69,69,69,57},['6']={60,74,73,73,48},['7']={1,113,9,5,3},['8']={54,73,73,73,54},['9']={6,73,73,41,30},['A']={126,17,17,17,126},['B']={127,73,73,73,54},['C']={62,65,65,65,34},['D']={127,65,65,34,28},['E']={127,73,73,73,65},['F']={127,9,9,9,1},['G']={62,65,73,73,122},['H']={127,8,8,8,127},['I']={0,65,127,65,0},['J']={32,64,65,63,1},['K']={127,8,20,34,65},['L']={127,64,64,64,64},['M']={127,2,12,2,127},['N']={127,4,8,16,127},['O']={62,65,65,65,62},['P']={127,9,9,9,6},['Q']={62,65,81,33,94},['R']={127,9,25,41,70},['S']={70,73,73,73,49},['T']={1,1,127,1,1},['U']={63,64,64,64,63},['V']={31,32,64,32,31},['W']={127,32,24,32,127},['X']={99,20,8,20,99},['Y']={3,4,120,4,3},['Z']={97,81,73,69,67},['-']={8,8,8,8,8},['_']={64,64,64,64,64},['.']={0,96,96,0,0},[':']={0,54,54,0,0},['/']={32,16,8,4,2},['+']={8,8,62,8,8},['!']={0,0,95,0,0},['?']={2,1,81,9,6},['(']={0,28,34,65,0},[')']={0,65,34,28,0},['[']={0,127,65,65,0},[']']={0,65,65,127,0},['=']={20,20,20,20,20},['<']={8,20,34,65,0},['>']={0,65,34,20,8},['*']={20,8,62,8,20},['#']={20,127,20,127,20},['$']={36,42,127,42,18},['@']={62,65,93,85,30},['&']={54,73,85,34,80},['\"']={0,7,0,7,0},['\'']={0,0,7,0,0},[';']={0,86,54,0,0},[',']={0,80,48,0,0}};

static const unsigned char lower_font[26][5] = {
	{32,84,84,84,120}, {127,72,68,68,56}, {56,68,68,68,32},
	{56,68,68,72,127}, {56,84,84,84,24}, {8,126,9,1,2},
	{24,164,164,164,124}, {127,8,4,4,120}, {0,68,125,64,0},
	{64,128,132,125,0}, {127,16,40,68,0}, {0,65,127,64,0},
	{124,4,120,4,120}, {124,8,4,4,120}, {56,68,68,68,56},
	{252,36,36,36,24}, {24,36,36,40,252}, {124,8,4,4,8},
	{72,84,84,84,32}, {4,63,68,64,32}, {60,64,64,32,124},
	{28,32,64,32,28}, {60,64,48,64,60}, {68,40,16,40,68},
	{28,160,160,160,124}, {68,100,84,76,68}
};

static void rect(int x,int y,int w,int h,unsigned int c){
	V*v;
	if(w<=0||h<=0) return;
	v=sceGuGetMemory(sizeof(V)*2);
	v[0]=(V){c,x,y,0,0};
	v[1]=(V){c,x+w,y+h,0,0};
	sceGuDrawArray(GU_SPRITES,GU_COLOR_8888|GU_VERTEX_16BIT|GU_TRANSFORM_2D,2,0,v);

}
static void rr(int x,int y,int w,int h,int r,unsigned int c){
	(void)r;
	rect(x,y,w,h,c);
}

static void txt(int x,int y,int s,unsigned int c,const char*t){
	for(;*t;t++){
		unsigned char ch=*t;
		const unsigned char *glyph;
		int a,b;
		if(ch==' '){
			x+=6*s;
			continue;
		}
		if(ch>='a'&&ch<='z') glyph=lower_font[ch-'a'];
		else{
			if(ch>=128) ch='?';
			glyph=font[ch];
		}
		for(a=0;a<5;a++) for(b=0;b<7;b++) if(glyph[a]&(1<<b)) rect(x+a*s,y+b*s,s,s,c);
		x+=6*s;
	}
}

static int cursor_visible(void){
	return ((sceKernelGetSystemTimeLow()/500000U)&1U)==0U;
}

static unsigned int hsv(int h,int sat,int val){
	int region=h/60,rem=(h-region*60)*255/60,p=val*(255-sat)/255,q=val*(255-sat*rem/255)/255,t=val*(255-sat*(255-rem)/255)/255,r=0,g=0,b=0;
	switch(region%6){
		case 0:
			r=val;
			g=t;b=p;
			break;
		case 1:
			r=q;
			g=val;
			b=p;
			break;
		case 2:
			r=p;
			g=val;
			b=t;
			break;
		case 3:
			r=p;
			g=q;
			b=val;
			break;
		case 4:
			r=t;
			g=p;
			b=val;
			break;
		default:
			r=val;
			g=p;
			b=q;
	}
	return ABGR(255,b,g,r);
}

void renderer_init(void){
	sceGuInit();
	sceGuStart(GU_DIRECT,dl);
	sceGuDrawBuffer(GU_PSM_8888,(void*)0,512);
	sceGuDispBuffer(W,H,(void*)0x88000,512);
	sceGuDepthBuffer((void*)0x110000,512);
	sceGuOffset(2048-W/2,2048-H/2);
	sceGuViewport(2048,2048,W,H);
	sceGuDisable(GU_DEPTH_TEST);
	sceGuDisable(GU_CULL_FACE);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuScissor(0,0,W,H);
	sceGuFinish();
	sceGuSync(0,0);
	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);
}

void renderer_shutdown(void){
	sceGuTerm();
}

void renderer_begin(int hue){
	accent=hsv(hue,175,245);
	bg=hsv(hue,125,42);
	panel=hsv(hue,90,67);
	soft=hsv(hue,70,92);
	sceGuStart(GU_DIRECT,dl);
	sceGuClearColor(bg);
	sceGuClear(GU_COLOR_BUFFER_BIT);
	rect(0,0,W,H,bg);
}

void renderer_end(void){
	sceGuFinish();
	sceGuSync(0,0);
	sceDisplayWaitVblankStart();
	sceGuSwapBuffers();
}

void renderer_header(const char*t,const char*s){
	txt(18,12,2,0xffffffff,t);
	txt(18,31,1,0xffd8d8e0,s);
	rect(16,47,448,1,soft);
}

static void folder_icon(int x,int y,unsigned int c){
	rect(x,y+2,11,7,c);
	rect(x+1,y,5,3,c);
	rect(x+1,y+3,9,1,0xff202028);
}

static void file_icon(int x,int y,unsigned int c){
	rect(x+1,y,8,10,c);
	rect(x+6,y,3,3,0xff202028);
	rect(x+3,y+4,4,1,0xff202028);
	rect(x+3,y+6,4,1,0xff202028);
}

void renderer_explorer(const Pane*p,int active,int idx,const char*status){
	int x=idx?242:4,y=51,w=234,i,start=p->scroll;
	unsigned int pc=active?soft:panel;
	rect(x,y,w,217,pc);
	rect(x,y,w,1,active?accent:soft);
	rect(x+3,y+4,w-6,18,0xff202028);
	txt(x+7,y+10,1,active?accent:0xffc8c8d0,p->path);
	for(i=0;i<14&&start+i<p->count;i++){
		const FileEntry*e=&p->entries[start+i];
		int yy=y+27+i*13;
		unsigned int tc=(i+start==p->selected&&active)?0xff101018:0xffffffff;
		if(i+start==p->selected) rect(x+3,yy-2,w-6,12,active?accent:0xff565660);
		if(p->marked[i+start]){
			rect(x+4,yy,7,7,0xffffffff);
			rect(x+6,yy+2,3,3,accent);
		}
		if(e->is_dir) folder_icon(x+13,yy-1,tc);
		else file_icon(x+14,yy-1,tc);
		txt(x+28,yy,1,tc,e->name);
	}
	if(!p->count) txt(x+8,y+34,1,0xffb8b8c0,"EMPTY");
	if(active&&status&&*status){
		rect(x+3,249,w-6,15,0xff202028);
		txt(x+7,253,1,0xffffffff,status);
	}
}

void renderer_menu(const char*title,const char*const*items,int n,int sel){
	const int visible=6;
	int first=(sel/visible)*visible;
	int shown=n-first;
	int i;
	char page[32];
	if(shown>visible) shown=visible;
	renderer_header(title,"PSPEXPLORER");
	rect(102,62,276,190,panel);
	rect(102,62,276,1,soft);
	rect(102,251,276,1,soft);
	for(i=0;i<shown;i++){
		int idx=first+i;
		int yy=76+i*27;
		if(idx==sel)rect(116,yy-5,248,22,accent);
		txt(130,yy,1,idx==sel?0xff101018:0xffffffff,items[idx]);
	}
	if(n>visible){
		snprintf(page,sizeof(page),"PAGE %d / %d",first/visible+1,(n+visible-1)/visible);
		txt(130,238,1,0xffd0d0d8,page);
	}
}

void renderer_hue(int hue){
	int x;
	renderer_header("APPEARANCE","HUE");
	for(x=35;x<445;x++) rect(x,110,1,22,hsv((x-35)*359/409,210,245));
	rect(34+hue*409/359,104,3,34,0xffffffff);
	txt(35,150,1,0xffffffff,"LEFT / RIGHT");
}

static void key_box(int x,int y,int w,const char*label,int selected){
	rect(x,y,w,15,selected?accent:0xff202028);
	txt(x+4,y+4,1,selected?0xff101018:0xffffffff,label);
}

static void keyboard_grid(int row,int col,int caps,int y){
	static const char*rows[6]={"qwertyuiop","asdfghjkl","zxcvbnm","1234567890","@#$_&-+()/","*\"':;!?.,"};
	static const int widths[6]={10,9,7,10,10,9}; int r,c; char one[2]={0};
	for(r=0;r<6;r++)for(c=0;c<widths[r];c++){
		int xx=16+c*28, yy=y+r*17;
		char ch=rows[r][c];
		if(caps&&ch>='a'&&ch<='z') ch=(char)(ch-'a'+'A');
		if(r==row&&c==col) rect(xx-2,yy-2,24,14,accent);
		one[0]=ch;
		txt(xx+5,yy+1,1,(r==row&&c==col)?0xff101018:0xffffffff,one);
	}
	{
		int yy=y+6*17;key_box(16,yy,34,caps?"c":"C",row==6&&col==0);
		key_box(54,yy,58,"BKSP",row==6&&col==1);
		key_box(116,yy,54,"SPACE",row==6&&col==2);
		key_box(174,yy,54,"ENTER",row==6&&col==3);
		key_box(232,yy,62,"CANCEL",row==6&&col==4);
		key_box(298,yy,38,"OK",row==6&&col==5);
	}
}

void renderer_text_editor(const char*name,const char*b,int cur,int row,int col,int caps){
	int i,line=0,column=0,start=0,cursor_x=-1,cursor_y=-1;
	char one[2]={0};
	renderer_header("Text Editor",name);
	rect(6,51,468,91,panel);
	rect(6,51,468,1,soft);
	if(cur>420) start=cur-420;
	for(i=start;b&&i<=cur+650;i++){
		if(i==cur){
			cursor_x=12+column*6;
			cursor_y=58+line*13;
		}
		if(!b[i]) break;
		if(b[i]=='\n'||column>74){
			line++;
			column=0;
			if(line>5) break;
			if(b[i]=='\n') continue;
		}
		if(line>5) break;
		one[0]=b[i];
		txt(12+column*6,58+line*13,1,0xffffffff,one);
		column++;
	}
	if(cursor_x>=0&&cursor_y>=0&&cursor_visible()) rect(cursor_x,cursor_y-1,1,9,accent);
	keyboard_grid(row,col,caps,148);
	txt(350,250,1,0xffd0d0d8,"L/R cursor");
}

void renderer_name_prompt(const char*title,const char*value,int row,int col,int caps,int folder_rules){
	int cursor_x;
	renderer_header(title,"PSPExplorer");
	rect(8,51,464,217,panel);
	rect(8,51,464,1,soft);
	txt(16,59,1,0xffd0d0d8,"Name");
	rect(16,70,448,22,0xff202028);
	if(value&&*value) txt(22,78,1,0xffffffff,value);
	cursor_x=22+(int)(value?strlen(value):0)*6;
	if(cursor_x>458) cursor_x=458;
	if(cursor_visible()) rect(cursor_x,76,1,10,accent);
	keyboard_grid(row,col,caps,101);
	if(folder_rules) txt(350,250,1,0xffc8c8d0,"A-Z a-z 0-9 .");
}

void renderer_confirm(const char*title,const char*message,const char*detail,int selected){
	char clipped[48]; size_t n;
	renderer_header(title,"PSPEXPLORER");
	rect(58,70,364,142,panel);
	rect(58,70,364,1,soft);
	txt(78,91,1,0xffffffff,message);
	if(detail&&*detail){
		n=strlen(detail);
		if(n>46) n=46;
		memcpy(clipped,detail,n);
		clipped[n]=0;
		txt(78,112,1,accent,clipped);
	}
	rect(82,158,110,28,selected==0?accent:0xff202028);
	txt(105,168,1,selected==0?0xff101018:0xffffffff,"CANCEL");
	rect(288,158,110,28,selected==1?accent:0xff202028);
	txt(316,168,1,selected==1?0xff101018:0xffffffff,"DELETE");
}

typedef struct{
	float u, v;
	float x, y, z;
	} ImageVertex;

void renderer_image(const char*n,const unsigned int*p,int iw,int ih,float zoom,int pan_x,int pan_y){
	const int vx=12,vy=53,vw=456,vh=190;
	int x,y;
	char z[32];
	ImageVertex *v;

	renderer_header("IMAGE VIEWER",n);
	rect(vx,vy,vw,vh,0xff101018);
	if(!p||iw<=0||ih<=0){
		txt(170,130,1,0xffffffff,"UNSUPPORTED IMAGE");
		return;
	}
	if(zoom<0.25f) zoom=0.25f;
	if(zoom>8.0f) zoom=8.0f;

	memset(image_view_texture,0,IMAGE_TEX_W*IMAGE_TEX_H*sizeof(image_view_texture[0]));
	for(y=0;y<vh;y++){
		const float sy=((float)(y-vh/2)/zoom)+(float)ih/2.0f+(float)pan_y;
		const int iy=(int)sy;
		unsigned int *dst=&image_view_texture[y*IMAGE_TEX_W];
		if(iy<0||iy>=ih) continue;
		for(x=0;x<vw;x++){
			const float sx=((float)(x-vw/2)/zoom)+(float)iw/2.0f+(float)pan_x;
			const int ix=(int)sx;
			if(ix>=0&&ix<iw)dst[x]=p[iy*iw+ix];
		}
	}
	sceKernelDcacheWritebackRange(image_view_texture,sizeof(image_view_texture));

	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(GU_PSM_8888,0,0,0);
	sceGuTexImage(0,IMAGE_TEX_W,IMAGE_TEX_H,IMAGE_TEX_W,image_view_texture);
	sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGBA);
	sceGuTexFilter(GU_NEAREST,GU_NEAREST);
	sceGuTexWrap(GU_CLAMP,GU_CLAMP);
	v=sceGuGetMemory(sizeof(ImageVertex)*2);
	v[0]=(ImageVertex){0.0f,0.0f,(float)vx,(float)vy,0.0f};
	v[1]=(ImageVertex){(float)vw,(float)vh,(float)(vx+vw),(float)(vy+vh),0.0f};
	sceGuDrawArray(GU_SPRITES,GU_TEXTURE_32BITF|GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,0,v);
	sceGuDisable(GU_TEXTURE_2D);

	snprintf(z,sizeof(z),"ZOOM %d%%",(int)(zoom*100.0f));
	txt(16,251,1,0xffffffff,z);
	txt(112,251,1,0xffd0d0d8,"L/R ZOOM  DPAD PAN  SQUARE RESET  CIRCLE BACK");
}

void renderer_about(void){
	renderer_header("ABOUT","PSPEXPLORER");
	rr(80,70,320,150,9,panel);
	txt(110,92,2,0xffffffff,"PSPEXPLORER");
	txt(110,124,1,0xffffffff,"VERSION 1.0.0");
	txt(110,142,1,0xffffffff,"AUTHOR: MISS VIOLIN MELODY");
	txt(110,160,1,0xffffffff,"HTTPS://VIOLINMELODY.NET");
	txt(110,184,1,0xffd0d0d8,"BUILT WITH PSPDEV / PSPSDK");
	txt(110,200,1,0xffd0d0d8,"NOT AFFILIATED WITH SONY");
}

void renderer_audio(const char *filename,const char *artist,const char *title,const float *wave,int points,int pos,int dur,int playing,const unsigned int *cover,int cw,int ch,int selected_button){
	int i;
	char line[280],timebuf[40];
	float progress=dur>0?(float)pos/(float)dur:0.0f;
	renderer_header("AUDIO PLAYER","");
	rect(10,51,460,216,panel);
	if(cover&&cw>0&&ch>0){
		int x,y;
		const int ox=20,oy=66,sz=92;
		for(y=0;y<sz;y+=2) for(x=0;x<sz;x+=2){
			int sx=x*cw/sz,sy=y*ch/sz;rect(ox+x,oy+y,2,2,cover[sy*cw+sx]);
		}
	}
	else {
		rect(20,66,92,92,0xff202028);
		txt(49,106,2,accent,"~");
	}
	if(artist&&*artist&&title&&*title) snprintf(line,sizeof(line),"%s - %s",artist,title);
	else if(title&&*title) snprintf(line,sizeof(line),"%s",title);
	else snprintf(line,sizeof(line),"%s",filename);
	txt(126,72,1,0xffc8c8d0,filename);txt(126,91,2,0xffffffff,line);
	rect(126,126,326,2,soft);
	if(wave&&points>0){
		for(i=0;i<points;i++){
			int h=(int)(wave[i]*42.0f);
			int x=126+i*326/points;
			unsigned int c=((float)i/points)<=progress?accent:0xff5a5a64;
			rect(x,150-h/2,1,h?h:1,c);
		}
	}
	rect(126,176,326,3,0xff4a4a54);
	rect(126,176,(int)(326*progress),3,accent);
	snprintf(timebuf,sizeof(timebuf),"%02d:%02d / %02d:%02d",pos/60000,(pos/1000)%60,dur/60000,(dur/1000)%60);txt(126,187,1,0xffffffff,timebuf);
	{
		const char *labels[3]={"<<",playing?"PAUSE":"PLAY",">>"};
		const int bx[3]={126,210,320};
		const int bw[3]={64,90,64};
		for(i=0;i<3;i++){
			unsigned int button_bg=(i==selected_button)?accent:0xff4a4256;
			unsigned int fg=(i==selected_button)?0xff101018:0xffffffff;
			rect(bx[i],207,bw[i],30,button_bg);
			txt(bx[i]+(bw[i]-(int)strlen(labels[i])*8)/2,217,1,fg,labels[i]);
		}
	}
}
