#include "audio.h"
#include <pspaudio.h>
#include <pspkernel.h>
#include <psputility_modules.h>
#include <pspmp3.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define FMT_WAV 1
#define FMT_MP3 2
#define CHUNK 1024

typedef struct{
	FILE *f;
	long data_off, data_size;
	int bits, block_align;
	} WavState;
	
static AudioPlayer *g_player;
static WavState g_wav;
static unsigned char mp3_stream[16384] __attribute__((aligned(64)));
static unsigned char mp3_pcm[18432] __attribute__((aligned(64)));
static int mp3_fd=-1, mp3_handle=-1, mp3_frames=0;

static unsigned int be32(const unsigned char *p){
	return ((unsigned)p[0]<<24)|((unsigned)p[1]<<16)|((unsigned)p[2]<<8)|p[3];
}

static unsigned int synchsafe(const unsigned char*p){
	return ((p[0]&127)<<21)|((p[1]&127)<<14)|((p[2]&127)<<7)|(p[3]&127);
}

static void base_name(const char*p,char*out,size_t n){
	const char*b=strrchr(p,'/');
	snprintf(out,n,"%s",b?b+1:p);
}

int audio_is_supported(const char*p){
	const char*e=strrchr(p,'.');
	return e&&(!strcasecmp(e,".wav")||!strcasecmp(e,".mp3"));
}

static void id3_text(char*out,size_t cap,const unsigned char*d,unsigned int n){
	unsigned int i=1,j=0;
	if(!n) return;
	if(d[0]==0||d[0]==3){
		for(i=1;i<n&&j+1<cap;i++){
			if(!d[i]) break;
			out[j++]=(char)d[i];
		}
	}
	else if(d[0]==1&&n>3){
		for(i=3;i+1<n&&j+1<cap;i+=2){
			unsigned char c=d[i+1];
			if(!c) break;
			out[j++]=(char)c;
		}
	}
	out[j]=0;
}

static void parse_id3(AudioPlayer*p,FILE*f,long*audio_start){
	unsigned char h[10];
	long start=ftell(f);
	if(fread(h,1,10,f)!=10||memcmp(h,"ID3",3)){
		fseek(f,start,SEEK_SET);
		*audio_start=start;
		return;
	}
	
	unsigned int total=synchsafe(h+6),used=0;
	while(used+10<=total){
		unsigned char fh[10];
		if(fread(fh,1,10,f)!=10) break;
		used+=10;
		if(!fh[0]) break;
		unsigned int sz=h[3]==4?synchsafe(fh+4):be32(fh+4);
		if(!sz||sz>total-used){
			break;
		}
		unsigned char*d=malloc(sz);
		if(!d){
			fseek(f,sz,SEEK_CUR);
			used+=sz;
			continue;
		}
		if(fread(d,1,sz,f)!=sz){
			free(d);
			break;
		}
		if(!memcmp(fh,"TIT2",4)) id3_text(p->title,sizeof(p->title),d,sz);
		else if(!memcmp(fh,"TPE1",4)) id3_text(p->artist,sizeof(p->artist),d,sz);
		else if(!memcmp(fh,"APIC",4)&&sz>16){
			unsigned int k=1;
			while(k<sz&&d[k]) k++;
			k++;
			if(k+2<sz){
				k++;
				while(k<sz&&d[k]) k++;
				k++;
				if(k<sz){
					const char*tmp=(sz-k>8&&d[k]==0x89&&d[k+1]==0x50&&d[k+2]==0x4e&&d[k+3]==0x47)?"ms0:/PSP/GAME/PSPExplorer/.cover.png":"ms0:/PSP/GAME/PSPExplorer/.cover.jpg";
					FILE*o=fopen(tmp,"wb");
					if(o){
						fwrite(d+k,1,sz-k,o);
						fclose(o);
						image_load(tmp,&p->cover);
						remove(tmp);
					}
				}
			}
		}
		free(d);
		used+=sz;
	}
	*audio_start=start+10+total;
	fseek(f,*audio_start,SEEK_SET);
}

static int wav_open(AudioPlayer*p){
	unsigned char h[12],ch[8];
	FILE*f=fopen(p->path,"rb");
	int fmt=0;
	if(!f) return -1;
	if(fread(h,1,12,f)!=12||memcmp(h,"RIFF",4)||memcmp(h+8,"WAVE",4)){
		fclose(f);
		return -1;
	}
	memset(&g_wav,0,sizeof(g_wav));
	g_wav.f=f;
	while(fread(ch,1,8,f)==8){
		unsigned int sz=(unsigned)ch[4]|((unsigned)ch[5]<<8)|((unsigned)ch[6]<<16)|((unsigned)ch[7]<<24);
		if(!memcmp(ch,"fmt ",4)){
			unsigned char b[32]={0};
			if(sz>sizeof(b)){
				fclose(f);
				return -1;
			}
			fread(b,1,sz,f);
			fmt=b[0]|(b[1]<<8);
			p->channels=b[2]|(b[3]<<8);
			p->sample_rate=b[4]|(b[5]<<8)|(b[6]<<16)|(b[7]<<24);
			g_wav.block_align=b[12]|(b[13]<<8);
			g_wav.bits=b[14]|(b[15]<<8);
		}
		else if(!memcmp(ch,"data",4)){
			g_wav.data_off=ftell(f);
			g_wav.data_size=sz;
			fseek(f,sz,SEEK_CUR);
		}
		else fseek(f,sz,SEEK_CUR);
		
		if(sz&1) fseek(f,1,SEEK_CUR);
		if(fmt!=1||g_wav.bits!=16||(p->channels!=1&&p->channels!=2)||!g_wav.data_size){
			fclose(f);
			return -1;
		}
		p->duration_ms=(int)((g_wav.data_size*1000LL)/(p->sample_rate*g_wav.block_align));
		fseek(f,g_wav.data_off,SEEK_SET);
		int i;
		long save=ftell(f);
		for(i=0;i<AUDIO_WAVEFORM_POINTS;i++){
			long off=g_wav.data_off+(long)((g_wav.data_size*(long long)i)/AUDIO_WAVEFORM_POINTS);
			short s[128];
			int j,c=0;
			float peak=0;
			off-=off%g_wav.block_align;
			fseek(f,off,SEEK_SET);
			c=(int)fread(s,sizeof(short),128,f);
			for(j=0;j<c;j++){
				float a=s[j]<0?-s[j]/32768.0f:s[j]/32767.0f;
				if(a>peak) peak=a;
			}
			p->waveform[i]=peak;
		}
		fseek(f,save,SEEK_SET);
	}
	return 0;
}

static int wav_thread(SceSize args,void*argp){
	(void)args;
	(void)argp;
	AudioPlayer*p=g_player;
	short buf[CHUNK*2];
	int ch=sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,CHUNK,PSP_AUDIO_FORMAT_STEREO);
	if(ch<0) return 0;
	while(!p->stop_requested){
		if(p->seek_request_ms>=0){
			long frame=(long)((p->seek_request_ms*(long long)p->sample_rate)/1000);
			long off=g_wav.data_off+frame*g_wav.block_align;
			if(off>g_wav.data_off+g_wav.data_size) off=g_wav.data_off+g_wav.data_size;
			fseek(g_wav.f,off,SEEK_SET);
			p->position_ms=p->seek_request_ms;
			p->seek_request_ms=-1;
		}
		if(!p->playing){
			sceKernelDelayThread(10000);
			continue;
		}
		int frames=0;
		if(p->channels==2) frames=(int)fread(buf,4,CHUNK,g_wav.f);
		else{
			short mono[CHUNK];
			int i;
			frames=(int)fread(mono,2,CHUNK,g_wav.f);
			for(i=frames-1;i>=0;i--){
				buf[i*2]=mono[i];
				buf[i*2+1]=mono[i];
			}
		}
		if(frames<=0){
			p->playing=0;
			continue;
		}
		if(frames<CHUNK) memset(buf+frames*2,0,(CHUNK-frames)*4);
		sceAudioOutputBlocking(ch,PSP_AUDIO_VOLUME_MAX,buf);
		p->position_ms=(int)(((ftell(g_wav.f)-g_wav.data_off)*1000LL)/(p->sample_rate*g_wav.block_align));
	}
	sceAudioChRelease(ch);
	return 0;
}


static int mp3_fill(void){
	unsigned char*dst;
	SceInt32 wr,pos;
	if(sceMp3GetInfoToAddStreamData(mp3_handle,&dst,&wr,&pos)<0) return 0;
	if(sceIoLseek32(mp3_fd,pos,PSP_SEEK_SET)<0) return 0;
	int rd=sceIoRead(mp3_fd,dst,wr);
	if(rd<=0) return 0;
	return sceMp3NotifyAddStreamData(mp3_handle,rd)>=0;
}

static int mp3_open(AudioPlayer*p){
	FILE*meta=fopen(p->path,"rb");
	long audio_start=0;
	if(meta){
		parse_id3(p,meta,&audio_start);
		fclose(meta);
	}
	if(sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC)<0) return -1;
	if(sceUtilityLoadModule(PSP_MODULE_AV_MP3)<0) return -1;
	if(sceMp3InitResource()<0) return -1;
	
	mp3_fd=sceIoOpen(p->path,PSP_O_RDONLY,0777);
	if(mp3_fd<0) return -1;
	
	SceOff end=sceIoLseek(mp3_fd,0,PSP_SEEK_END);
	SceMp3InitArg a;
	a.mp3StreamStart=audio_start;
	a.mp3StreamEnd=end;
	a.mp3Buf=mp3_stream;
	a.mp3BufSize=sizeof(mp3_stream);
	a.pcmBuf=mp3_pcm;
	a.pcmBufSize=sizeof(mp3_pcm);
	mp3_handle=sceMp3ReserveMp3Handle(&a);
	if(mp3_handle<0) return -1;
	if(!mp3_fill()||sceMp3Init(mp3_handle)<0) return -1;
	
	p->sample_rate=sceMp3GetSamplingRate(mp3_handle);
	p->channels=sceMp3GetMp3ChannelNum(mp3_handle);
	mp3_frames=sceMp3GetFrameNum(mp3_handle);
	if(mp3_frames>0) p->duration_ms=(int)((mp3_frames*1152LL*1000)/p->sample_rate);
	
	return 0;
}

static int mp3_thread(SceSize args,void*argp){
	(void)args;
	(void)argp;
	AudioPlayer*p=g_player;
	int src=-1;
	while(!p->stop_requested){
		if(p->seek_request_ms>=0&&mp3_frames>0){
			unsigned frame=(unsigned)((p->seek_request_ms*(long long)mp3_frames)/(p->duration_ms?p->duration_ms:1));
			sceMp3ResetPlayPositionByFrame(mp3_handle,frame);
			p->position_ms=p->seek_request_ms;
			p->seek_request_ms=-1;
		}
		if(!p->playing){
			sceKernelDelayThread(10000);
			continue;
		}
		if(sceMp3CheckStreamDataNeeded(mp3_handle)>0) mp3_fill();
		
		short*buf=NULL;
		int bytes=sceMp3Decode(mp3_handle,&buf);
		if(bytes<=0){
			p->playing=0;
			continue;
		}
		
		int samples=bytes/(2*p->channels);
		if(src<0) src=sceAudioSRCChReserve(samples,p->sample_rate,p->channels);
		if(src>=0){
			sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX,buf);
			int total=sceMp3GetSumDecodedSample(mp3_handle);
			p->position_ms=(int)(total*1000LL/p->sample_rate);
			int idx=p->duration_ms?(p->position_ms*AUDIO_WAVEFORM_POINTS/p->duration_ms):0;
			if(idx>=0&&idx<AUDIO_WAVEFORM_POINTS){
				int i;
				float peak=0;
				for(i=0;i<samples*p->channels;i++){
					float v=buf[i]<0?-buf[i]/32768.0f:buf[i]/32767.0f;
					if(v>peak)peak=v;
				}
				if(peak>p->waveform[idx]) p->waveform[idx]=peak;
			}
		}
	}

	if(src>=0) sceAudioSRCChRelease();
	return 0;
}

int audio_player_open(AudioPlayer*p,const char*path){
	memset(p,0,sizeof(*p));
	snprintf(p->path,sizeof(p->path),"%s",path);
	base_name(path,p->filename,sizeof(p->filename));
	p->seek_request_ms=-1;
	if(strrchr(path,'.')&&!strcasecmp(strrchr(path,'.'),".wav")){
		p->format=FMT_WAV;
		if(wav_open(p)!=0) return-1;
	}
	else if(strrchr(path,'.')&&!strcasecmp(strrchr(path,'.'),".mp3")){
		p->format=FMT_MP3;
		if(mp3_open(p)!=0) return -1;
	}
	else{
		return -1;
	}
	
	p->loaded=1;
	p->playing=1;
	g_player=p;
	p->thread_id=sceKernelCreateThread("audio_player",p->format==FMT_MP3?mp3_thread:wav_thread,0x18,0x8000,PSP_THREAD_ATTR_USER,NULL);
	if(p->thread_id<0){
		audio_player_close(p);
		return -1;
	}
	
	sceKernelStartThread(p->thread_id,0,NULL);
	return 0;
}

void audio_player_toggle(AudioPlayer*p){
	if(p&&p->loaded) p->playing=!p->playing;
}

void audio_player_seek(AudioPlayer*p,int delta){
	int v;
	if(!p||!p->loaded) return;
	v=p->position_ms+delta;
	if(v<0) v=0;
	if(v>p->duration_ms) v=p->duration_ms;
	p->seek_request_ms=v;
}

void audio_player_close(AudioPlayer*p){
	if(!p) return;
	
	p->stop_requested=1;
	if(p->thread_id>0){
		sceKernelWaitThreadEnd(p->thread_id,NULL);
		sceKernelDeleteThread(p->thread_id);
	}
	if(g_wav.f){
		fclose(g_wav.f);
		g_wav.f=NULL;
	}
	if(mp3_handle>=0){
		sceMp3ReleaseMp3Handle(mp3_handle);
		mp3_handle=-1;
	}
	if(mp3_fd>=0){
		sceIoClose(mp3_fd);
		mp3_fd=-1;
	}
	if(p->format==FMT_MP3){
		sceMp3TermResource();
		sceUtilityUnloadModule(PSP_MODULE_AV_MP3);
		sceUtilityUnloadModule(PSP_MODULE_AV_AVCODEC);
	}
	
	image_free(&p->cover);
	memset(p,0,sizeof(*p));
	g_player=NULL;
}
