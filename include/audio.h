#ifndef PSPEXPLORER_AUDIO_H
#define PSPEXPLORER_AUDIO_H
#include "image.h"
#define AUDIO_WAVEFORM_POINTS 180

typedef struct{
    char path[512], filename[256], artist[128], title[128];
    int format, playing, loaded, duration_ms, position_ms, sample_rate, channels;
    float waveform[AUDIO_WAVEFORM_POINTS];
    Image cover;
    int thread_id, stop_requested, seek_request_ms;
	} AudioPlayer;
	
int audio_is_supported(const char *path);
int audio_player_open(AudioPlayer *p, const char *path);
void audio_player_toggle(AudioPlayer *p);
void audio_player_seek(AudioPlayer *p, int delta_ms);
void audio_player_close(AudioPlayer *p);

#endif
