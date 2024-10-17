#include "config.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

static struct {
	entry_type t;
	const char *s;
} tab_mappings[] = {
	{ENTRY_SINKINPUT, "2"},
	{ENTRY_SINKINPUT, "playback"},
	{ENTRY_SINK, "0"},
	{ENTRY_SINK, "output"},
	{ENTRY_SOURCEOUTPUT, "3"},
	{ENTRY_SOURCEOUTPUT, "recording"},
	{ENTRY_SOURCE, "1"},
	{ENTRY_SOURCE, "input"},
	{ENTRY_CARD, "4"},
	{ENTRY_CARD, "cards"},
};

static bool has_prefix(const char *str, const char *prefix) {
	while (*str && *prefix && *str++ == *prefix++)
		;
	return !*prefix;
}

int config_load(Config *config, const char *path) {
	memset(config, 0, sizeof(*config));

	const char *keynames[KEY_MAX];
	for (int i = 0; i < KEY_MAX; i++)
		keynames[i] = keyname(i);

	FILE *f = fopen(path, "rb");
	if (f == NULL)
		return -1;

	fseek(f, 0, SEEK_END);
	long length = ftell(f);
	fseek(f, 0, SEEK_SET);

	char *text = (char *)malloc(length + 1);
	assert(text != NULL);

	text[fread(text, 1, length, f)] = 0;

	fclose(f);

	char *save = NULL;
	for (char *line = strtok_r(text, "\n", &save); line != NULL; line = strtok_r(NULL, "\n", &save)) {
		char *comment = strchr(line, ';');
		if (comment != NULL)
			*comment = '\0';

		if (has_prefix(line, "set ")) {
			// TODO
			continue;
		}
		if (has_prefix(line, "bind ")) {
			char *key = line + sizeof("bind");
			char *action = strchr(key, ' ');
			*action++ = '\0';
			int keycode = -1;
			for (int i = 0; i < KEY_MAX; i++) {
				if (keynames[i] && strcmp(key, keynames[i]) == 0) {
					keycode = i;
					break;
				}
			}
			if (keycode == -1) {
				assert(0 && "invalid keycode");
				continue;
			}

			char *arg = strchr(action, ' ');
			if (arg != NULL)
				*arg++ = '\0';

			Action *info = &config->keymap[keycode];
			if (strcmp(action, "quit") == 0) {
				info->type = ACTION_QUIT;
				continue;
			}
			if (strcmp(action, "select-tab") == 0) {
				assert(arg != NULL);
				int idx = -1;
				for (size_t i = 0; i < sizeof(tab_mappings) / sizeof(*tab_mappings); i++) {
					if (strcmp(tab_mappings[i].s, arg) == 0) {
						idx = i;
						break;
					}
				}
				assert(idx != -1);
				info->type = ACTION_SELECT_TAB;
				info->data.tab = tab_mappings[idx].t;
				continue;
			}
			if (strcmp(action, "set-volume") == 0 || strcmp(action, "add-volume") == 0) {
				assert(arg != NULL);
				char *end;
				double value = strtod(arg, &end);
				assert(end == arg + strlen(arg));
				info->type = strcmp(action, "set-volume") == 0 ? ACTION_VOLUME_SET : ACTION_VOLUME_ADD;
				info->data.volume = (float)value;
				continue;
			}
			if(strcmp(action, "select-next") == 0) {
				info->type = ACTION_ENTRY_NEXT;
				continue;
			}
			if(strcmp(action, "select-prev") == 0) {
				info->type = ACTION_ENTRY_PREV;
				continue;
			}
			if(strcmp(action, "cycle-next") == 0) {
				info->type = ACTION_DEVICE_NEXT;
				continue;
			}
			if(strcmp(action, "cycle-prev") == 0) {
				info->type = ACTION_DEVICE_PREV;
				continue;
			}
			if(strcmp(action, "toggle-mute") == 0) {
				info->type = ACTION_MUTE_TOGGLE;
				continue;
			}
			if(strcmp(action, "toggle-lock") == 0) {
				info->type = ACTION_LOCK_TOGGLE;
				continue;
			}
			// TODO: tab cycle?
			continue;
		}
		/*
		if(strlen(line) > 0)
			fprintf(stderr, "line: %s\n", line);
			*/
	}
	free(text);

	return 0;
}

void config_default(Config *config) {
	config->keymap['q'] = (Action){.type = ACTION_QUIT};
	for(int i = 0; i < 10; i++)
		config->keymap['0' + i] = (Action){.type = ACTION_VOLUME_SET, .data = {.volume = i == 0 ? 1.0f : i * 0.1f}};
	config->keymap['h'] = (Action){.type = ACTION_VOLUME_ADD, .data = {.volume = -0.05f}};
	config->keymap['l'] = (Action){.type = ACTION_VOLUME_ADD, .data = {.volume = 0.05f}};
	config->keymap['j'] = (Action){.type = ACTION_ENTRY_NEXT};
	config->keymap['k'] = (Action){.type = ACTION_ENTRY_PREV};
	for(int i = 0; i <= ENTRY_CARD; i++)
		config->keymap[KEY_F(i + 1)] = (Action){.type = ACTION_SELECT_TAB, .data = {.tab = (entry_type)i}};
	config->keymap['s'] = (Action){.type = ACTION_DEVICE_NEXT};
	config->keymap['S'] = (Action){.type = ACTION_DEVICE_PREV};
	config->keymap['c'] = (Action){.type = ACTION_LOCK_TOGGLE};
	config->keymap['m'] = (Action){.type = ACTION_MUTE_TOGGLE};
}
