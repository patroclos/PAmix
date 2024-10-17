#ifndef _DRAW_H
#define _DRAW_H
#include <pulse/volume.h>

void draw_volume_bar(int y, int x, int width, pa_volume_t volume);

#endif
