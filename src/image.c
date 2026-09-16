#include "image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <strings.h>
#include <jpeglib.h>

static int png_load(const char*p,Image*i){
	FILE*f=fopen(p,"rb");
	png_structp png;
	png_infop info;
	png_bytep*rows;
	int y;
	if(!f) return -1;
	
	png=png_create_read_struct(PNG_LIBPNG_VER_STRING,0,0,0);
	info=png_create_info_struct(png);
	if(!png||!info||setjmp(png_jmpbuf(png))){
		if(png) png_destroy_read_struct(&png,&info,0);
		fclose(f);
		return -1;
	}
	
	png_init_io(png,f);
	png_read_info(png,info);
	i->w=png_get_image_width(png,info);
	i->h=png_get_image_height(png,info);
	if(i->w>2048||i->h>2048){
		png_destroy_read_struct(&png,&info,0);
		fclose(f);
		return -1;
	}
	
	png_set_strip_16(png);
	png_set_palette_to_rgb(png);
	png_set_expand_gray_1_2_4_to_8(png);
	png_set_tRNS_to_alpha(png);
	png_set_filler(png,0xff,PNG_FILLER_AFTER);
	png_set_gray_to_rgb(png);
	png_read_update_info(png,info);
	i->pixels=malloc(i->w*i->h*4);
	rows=malloc(sizeof(png_bytep)*i->h);
	for(y=0;y<i->h;y++) rows[y]=(png_bytep)i->pixels+y*i->w*4;
	png_read_image(png,rows);
	free(rows);
	png_destroy_read_struct(&png,&info,0);
	fclose(f);
	return 0;
}
 
static int jpg_load(const char*p,Image*i){
	FILE*f=fopen(p,"rb");
	struct jpeg_decompress_struct c;
	struct jpeg_error_mgr e;
	JSAMPARRAY row;
	int y;
	if(!f) return-1;
	
	c.err=jpeg_std_error(&e);
	jpeg_create_decompress(&c);
	jpeg_stdio_src(&c,f);
	jpeg_read_header(&c,TRUE);
	jpeg_start_decompress(&c);
	i->w=c.output_width;
	i->h=c.output_height;
	if(i->w>2048||i->h>2048){
		jpeg_destroy_decompress(&c);
		fclose(f);
		return -1;
	}
	
	i->pixels=malloc(i->w*i->h*4);
	row=(*c.mem->alloc_sarray)((j_common_ptr)&c,JPOOL_IMAGE,i->w*c.output_components,1);
	for(y=0;y<i->h;y++){
		int x;
		jpeg_read_scanlines(&c,row,1);
		for(x=0;x<i->w;x++){
			unsigned char*r=&row[0][x*c.output_components];
			unsigned char R=r[0],G=c.output_components>2?r[1]:r[0],B=c.output_components>2?r[2]:r[0];
			i->pixels[y*i->w+x]=0xff000000|(B<<16)|(G<<8)|R;
		}
	}
	jpeg_finish_decompress(&c);
	jpeg_destroy_decompress(&c);
	fclose(f);
	return 0;
}

#pragma pack(push,1)
typedef struct{
	unsigned short type;
	unsigned int size;
	unsigned short r1,r2;
	unsigned int off;
	}BH;

typedef struct{
	unsigned int sz;
	int w,h;
	unsigned short planes,bpp;
	unsigned int comp,img;
	int xp,yp;
	unsigned int used,important;
	}BI;
	
#pragma pack(pop)
static int bmp_load(const char*p,Image*i){
	FILE*f=fopen(p,"rb");
	BH h;
	BI n;
	int x,y,row;
	if(!f) return -1;
	if(fread(&h,sizeof(h),1,f)!=1||fread(&n,sizeof(n),1,f)!=1||h.type!=0x4d42||(n.bpp!=24&&n.bpp!=32)||n.comp){
		fclose(f);
		return -1;
	}
	
	i->w=n.w;
	i->h=n.h<0?-n.h:n.h;
	i->pixels=malloc(i->w*i->h*4);
	row=((i->w*n.bpp+31)/32)*4;
	for(y=0;y<i->h;y++){
		int dy=n.h>0?i->h-1-y:y;
		fseek(f,h.off+y*row,SEEK_SET);
		for(x=0;x<i->w;x++){
			unsigned char bgr[4];
			fread(bgr,1,n.bpp/8,f);
			i->pixels[dy*i->w+x]=0xff000000|(bgr[0]<<16)|(bgr[1]<<8)|bgr[2];
		}
	}
	fclose(f);
	return 0;
}

int image_load(const char*p,Image*i){
	const char*e=strrchr(p,'.');
	memset(i,0,sizeof(*i));
	if(!e) return -1;
	if(!strcasecmp(e,".png")) return png_load(p,i);
	if(!strcasecmp(e,".jpg")||!strcasecmp(e,".jpeg")) return jpg_load(p,i);
	if(!strcasecmp(e,".bmp")) return bmp_load(p,i);
	return -1;
}

void image_free(Image*i){
	free(i->pixels);
	memset(i,0,sizeof(*i));
}
