#include "settings.h"
#include <stdio.h>
#define FILE_NAME "ms0:/PSP/GAME/PSPExplorer/settings.dat"

void settings_load(Settings*s){
	FILE*f;
	s->hue=210;
	f=fopen(FILE_NAME,"rb");
	if(f){
		fread(&s->hue,sizeof(s->hue),1,f);
		fclose(f);
	}
	if(s->hue<0||s->hue>359) s->hue=210;
}

void settings_save(const Settings*s){
	FILE*f=fopen(FILE_NAME,"wb");
	if(f){
		fwrite(&s->hue,sizeof(s->hue),1,f);
		fclose(f);
	}
}
