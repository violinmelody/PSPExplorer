#ifndef PSPEXPLORER_SETTINGS_H
#define PSPEXPLORER_SETTINGS_H

typedef struct {
	int hue;
	} Settings;
	
void settings_load(Settings*s);
void settings_save(const Settings*s);

#endif
