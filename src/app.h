#ifndef _APP_H
#define _APP_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <pulse/pulseaudio.h>
#include <stdatomic.h>

typedef enum {
	ENTRY_SINKINPUT,
	ENTRY_SOURCEOUTPUT,
	ENTRY_SINK,
	ENTRY_SOURCE,
	ENTRY_CARD
} entry_type;

typedef struct {
	const char *name;
	const char *description;
} NameDesc;

typedef struct {
	NameDesc *items;
	size_t len;
	size_t cap;
	int current;
} NameDescs;

union EntryData {
	// sink/source of sinkinput and sourceoutput entries
	// TODO: do we put device names in here?
	struct {
		uint32_t index;
		const char *name;
	} device;
	// ports of sink and source entries
	NameDescs ports;
	// profiles of card entries
	NameDescs profiles;
};

typedef struct {
	entry_type type;
	const char *name;
	uint32_t pa_index;
	pa_cvolume volume;
	pa_channel_map channel_map;
	pa_proplist *props;
	pa_stream *monitor_stream;
	uint32_t monitor_index;
	float peak;
	bool muted;
	bool corked;
	bool marked;
	bool volume_lock;

	union EntryData data;
} Entry;

typedef struct {
	Entry *items;
	size_t len;
	size_t cap;
} Entries;

typedef struct {
	int keycode;
	const char *keyname;
} InputEvent;

typedef struct {
	InputEvent *items;
	size_t len;
	size_t cap;
} InputQueue;

typedef struct {
	pa_context *pa_context;
	pa_threaded_mainloop *pa_mainloop;
	Entries entries;
	entry_type entry_page;
	int selected_entry;
	int selected_channel;
	int scroll;
	pthread_mutex_t mutex;
	atomic_bool should_refresh;
	atomic_bool resized;
	//bool resized;
	bool new_peaks;
	bool running;
	InputQueue input_queue;
} App;

extern App app;

void app_init(App *app, pa_context *context, pa_threaded_mainloop *mainloop);
bool app_refresh_entries(App *app);

void entry_free(Entry *entry);
#endif
