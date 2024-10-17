#ifndef _CONFIG_H
#define _CONFIG_H

#include "app.h"
#include <ncurses.h>

typedef enum {
	ACTION_NONE = 0,
	ACTION_QUIT,
	ACTION_SELECT_TAB,
	ACTION_MUTE_TOGGLE,
	ACTION_LOCK_TOGGLE,
	ACTION_ENTRY_NEXT,
	ACTION_ENTRY_PREV,
	ACTION_VOLUME_ADD,
	ACTION_VOLUME_SET,
	ACTION_DEVICE_NEXT,
	ACTION_DEVICE_PREV,
} ActionType;

typedef struct {
	ActionType type;
	union {
		entry_type tab;
		float volume;
	} data;
} Action;

typedef struct {
	Action keymap[KEY_MAX];
} Config;

int config_load(Config *config, const char *path);
void config_default(Config *config);

#endif
