#ifndef PSPEXPLORER_IMAGE_H
#define PSPEXPLORER_IMAGE_H

typedef struct{
	unsigned int *pixels;
	int w,h;
	} Image;

int image_load(const char*path,Image*i);
void image_free(Image*i);

#endif
