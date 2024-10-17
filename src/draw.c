#include "draw.h"
#include <wchar.h>
#include <ncurses.h>
#include <assert.h>

static wchar_t braille[] = { L' ', L'\u2802', L'\u2806', L'\u2807', L'\u280f', L'\u281f', L'\u283f' };
static int n_braille = (int)(sizeof(braille) / sizeof(*braille));

void draw_volume_bar(int y, int x, int width, pa_volume_t volume) {
	int segments = width - 2;
	if (segments <= 0)
		return;
	int fill = (int)((float)volume / (float)(PA_VOLUME_NORM * 1.5) * (float)segments);
	if(fill > segments)
		fill = segments;
	mvaddstr(y, x++, "[");
	mvaddstr(y, x + segments, "]");

	wchar_t buf[segments + 1];
	buf[segments] = 0;
	for (int i = 0; i < segments; i++)
		buf[i] = i >= fill ? L' ' : braille[n_braille-1];

	if(volume != PA_VOLUME_MUTED && volume != PA_VOLUME_NORM) {
		int segval = (PA_VOLUME_NORM * 1.5) / segments;
		int subsegval = segval / (n_braille - 1);
		int idx = (volume % segval) / subsegval;
		assert(idx <= n_braille - 1);
		buf[fill] = braille[idx];
	}

	int indexA = (int)(segments * ((double) 1 / 3));
	int indexB = (int)(segments * ((double) 2 / 3));
	attron(COLOR_PAIR(1));
	mvaddnwstr(y, x, buf, indexA);
	attroff(COLOR_PAIR(1));
	attron(COLOR_PAIR(2));
	mvaddnwstr(y, x + indexA, buf + indexA, indexB - indexA);
	attroff(COLOR_PAIR(2));
	attron(COLOR_PAIR(3));
	mvaddnwstr(y, x + indexB, buf + indexB, segments - indexB);
	attroff(COLOR_PAIR(3));
}
