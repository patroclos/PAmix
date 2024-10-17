#include "app.h"
#include "da.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void entry_data_free(Entry *entry);

App app = {0};

struct entry_indices {
	uint32_t *indices;
	int count;
};
// this cmp function splits entries into two groups: corked and uncorked.  The sorting is stabelized by passing the
// original order as the context argument, so order within groups doesnt change, only the uncorked streams are brought
// to the front.
// data should be a `struct indices {int len; uint32_t *indices;}`
static int cmp_entry(const void *pa, const void *pb, void *data) {
	const Entry *a = pa;
	const Entry *b = pb;
	assert(a->pa_index != b->pa_index);
	if(a->corked != b->corked) {
		return a->corked - b->corked;
	}
	struct entry_indices *indices = data;
	int ia = -1;
	int ib = -1;
	for(int i = 0; i < indices->count; i++) {
		if(indices->indices[i] == a->pa_index)
			ia = i;
		else if(indices->indices[i] == b->pa_index)
			ib = i;

		if(ia != -1 && ib != -1)
			break;
	}
	assert(ia != -1);
	assert(ib != -1);
	return ia - ib;
}

static void cull_entries(Entries *ents) {
	for (int i = (int)ents->len - 1; i >= 0; i--) {
		Entry *ent = &ents->items[i];
		if (!ents->items[i].marked)
			continue;
		entry_free(ent);
		memmove(ents->items + i, ents->items + i + 1, (ents->len - i - 1) * sizeof(Entry));
		ents->len--;
	}
}

static void cb_monitor_read(pa_stream *stream, size_t nbytes, void *pdata) {
	uint32_t index = (uintptr_t)pdata;
	const void *data;
	int err = pa_stream_peek(stream, &data, &nbytes);
	if (err != 0) {
		return;
	}
	assert(nbytes >= sizeof(float));
	assert((nbytes % sizeof(float)) == 0);
	float last_peak = ((float *)data)[nbytes / sizeof(float) - 1];

	pa_stream_drop(stream);

	pthread_mutex_lock(&app.mutex);
	for (size_t i = 0; i < app.entries.len; i++) {
		Entry *ent = &app.entries.items[i];
		if (ent->pa_index == index || ent->monitor_index == index) {
			if (ent->monitor_stream != stream)
				break;
			assert(ent->monitor_stream == stream);
			ent->peak = last_peak;
			app.new_peaks = true;
			pa_threaded_mainloop_signal(app.pa_mainloop, false);
			break;
		}
	}
	pthread_mutex_unlock(&app.mutex);
}

static void cb_monitor_state(pa_stream *stream, void *data) {
	(void)data;
	pa_stream_state_t state = pa_stream_get_state(stream);
	if (state == PA_STREAM_FAILED || state == PA_STREAM_TERMINATED) {
		pthread_mutex_lock(&app.mutex);
		for (size_t i = 0; i < app.entries.len; i++) {
			Entry *ent = &app.entries.items[i];
			if (ent->monitor_stream == stream) {
				ent->monitor_stream = NULL;
				ent->peak = 0;
				break;
			}
		}
		pthread_mutex_unlock(&app.mutex);
		return;
	}

	if (state == PA_STREAM_READY) {
		pthread_mutex_lock(&app.mutex);
		int idx = -1;
		for (size_t i = 0; i < app.entries.len; i++) {
			Entry *ent = &app.entries.items[i];
			if (ent->monitor_stream == stream) {
				idx = i;
				break;
			}
		}
		pthread_mutex_unlock(&app.mutex);
		if (idx == -1) {
			int err = pa_stream_disconnect(stream);
			pa_stream_unref(stream);
			// TODO: remove log
			fprintf(stderr, "destroyed orphan stream %d\n", err);
		}
	}
}

static pa_stream *create_monitor(pa_context *ctx, uint32_t monitor_stream, uint32_t device) {
	char stream_name[32];
	snprintf(stream_name, sizeof(stream_name) - 1, "PeakMonitor %d", monitor_stream);

	pa_sample_spec spec = {.rate = 40, .format = PA_SAMPLE_FLOAT32LE, .channels = 1};
	pa_proplist *props = pa_proplist_new();
	// hide monitor stream from pavucontrol
	pa_proplist_sets(props, PA_PROP_APPLICATION_ID, "org.PulseAudio.pavucontrol");
	pa_stream *stream = pa_stream_new_with_proplist(ctx, stream_name, &spec, NULL, props);
	pa_proplist_free(props);
	assert(stream != NULL);

	char devname[16];
	if (monitor_stream != PA_INVALID_INDEX) {
		assert(device == PA_INVALID_INDEX);
		int err = pa_stream_set_monitor_stream(stream, monitor_stream);
		if (err != 0) {
			fprintf(stderr, "failed to set peakdetect monitor-stream: %s\n", pa_strerror(err));
			pa_stream_unref(stream);
			return NULL;
		}
	} else {
		assert(device != PA_INVALID_INDEX);
		sprintf(devname, "%u", device);
	}

	pa_stream_set_read_callback(stream, &cb_monitor_read, (void *)(uintptr_t)(monitor_stream != PA_INVALID_INDEX ? monitor_stream : device));
	pa_stream_set_state_callback(stream, &cb_monitor_state, NULL);

	pa_stream_flags_t flags = (pa_stream_flags_t)(PA_STREAM_DONT_MOVE | PA_STREAM_PEAK_DETECT | PA_STREAM_ADJUST_LATENCY);
	pa_buffer_attr bufattr = {.maxlength = 128, .fragsize = sizeof(float)};
	int err = pa_stream_connect_record(stream, device == PA_INVALID_INDEX ? NULL : devname, &bufattr, flags);
	if (err != 0) {
		pa_stream_unref(stream);
		fprintf(stderr, "connect record fail: %s\n", pa_strerror(err));
		return NULL;
	}

	return stream;
}

static int find_entry_with_index(uint32_t index, entry_type type) {
	for (int i = 0; i < (int)app.entries.len; i++) {
		Entry ent = app.entries.items[i];
		if (ent.pa_index == index && ent.type == type)
			return i;
	}
	return -1;
}

uint32_t pa_entry_index(const void *info, entry_type type) {
	switch (type) {
	case ENTRY_SINKINPUT:
		return ((const pa_sink_input_info *)info)->index;
	case ENTRY_SOURCEOUTPUT:
		return ((const pa_source_output_info *)info)->index;
	case ENTRY_SINK:
		return ((const pa_sink_info *)info)->index;
	case ENTRY_SOURCE:
		return ((const pa_source_info *)info)->index;
	case ENTRY_CARD:
		return ((const pa_card_info *)info)->index;
	}
	__builtin_unreachable();
}
const char *pa_entry_name(const void *info, entry_type type) {
	switch (type) {
	case ENTRY_SINKINPUT:
		return ((const pa_sink_input_info *)info)->name;
	case ENTRY_SOURCEOUTPUT:
		return ((const pa_source_output_info *)info)->name;
	case ENTRY_SINK:
		return ((const pa_sink_info *)info)->name;
	case ENTRY_SOURCE:
		return ((const pa_source_info *)info)->name;
	case ENTRY_CARD:
		return ((const pa_card_info *)info)->name;
	}
	__builtin_unreachable();
}
pa_cvolume pa_entry_volume(const void *info, entry_type type) {
	switch (type) {
	case ENTRY_SINKINPUT:
		return ((const pa_sink_input_info *)info)->volume;
	case ENTRY_SOURCEOUTPUT:
		return ((const pa_source_output_info *)info)->volume;
	case ENTRY_SINK:
		return ((const pa_sink_info *)info)->volume;
	case ENTRY_SOURCE:
		return ((const pa_source_info *)info)->volume;
	case ENTRY_CARD:
		return (pa_cvolume){.channels = 0};
	}
	__builtin_unreachable();
}
bool pa_entry_corked(const void *info, entry_type type) {
	switch (type) {
	case ENTRY_SINKINPUT:
		return ((const pa_sink_input_info *)info)->corked;
	case ENTRY_SOURCEOUTPUT:
		return ((const pa_source_output_info *)info)->corked;
	case ENTRY_SINK:
		return false;
	case ENTRY_SOURCE:
		return false;
	case ENTRY_CARD:
		return false;
	}
	__builtin_unreachable();
}
bool pa_entry_mute(const void *info, entry_type type) {
	switch (type) {
	case ENTRY_SINKINPUT:
		return ((const pa_sink_input_info *)info)->mute;
	case ENTRY_SOURCEOUTPUT:
		return ((const pa_source_output_info *)info)->mute;
	case ENTRY_SINK:
		return ((const pa_sink_info *)info)->mute;
	case ENTRY_SOURCE:
		return ((const pa_source_info *)info)->mute;
	case ENTRY_CARD:
		return false;
	}
	__builtin_unreachable();
}
pa_proplist *pa_entry_proplist(const void *info, entry_type type) {
	switch (type) {
	case ENTRY_SINKINPUT:
		return ((const pa_sink_input_info *)info)->proplist;
	case ENTRY_SOURCEOUTPUT:
		return ((const pa_source_output_info *)info)->proplist;
	case ENTRY_SINK:
		return ((const pa_sink_info *)info)->proplist;
	case ENTRY_SOURCE:
		return ((const pa_source_info *)info)->proplist;
	case ENTRY_CARD:
		return ((const pa_card_info *)info)->proplist;
	}
	__builtin_unreachable();
}

pa_channel_map pa_entry_channel_map(const void *info, entry_type type) {
	switch (type) {
	case ENTRY_SINKINPUT:
		return ((const pa_sink_input_info *)info)->channel_map;
	case ENTRY_SOURCEOUTPUT:
		return ((const pa_source_output_info *)info)->channel_map;
	case ENTRY_SINK:
		return ((const pa_sink_info *)info)->channel_map;
	case ENTRY_SOURCE:
		return ((const pa_source_info *)info)->channel_map;
	case ENTRY_CARD:
		return (pa_channel_map){.channels = 0};
	}
	__builtin_unreachable();
}

uint32_t pa_entry_monitor_index(const void *info, entry_type type) {
	switch (type) {
	case ENTRY_SINK:
		return ((const pa_sink_info *)info)->monitor_source;
	case ENTRY_SOURCE:
		return ((const pa_source_info *)info)->index;
	case ENTRY_SOURCEOUTPUT:
		return ((const pa_source_output_info *)info)->source;
	default:
		return PA_INVALID_INDEX;
	}
}

void apply_entry_data(union EntryData *data, const void *info, entry_type type) {
	switch (type) {
	case ENTRY_SINKINPUT:
	case ENTRY_SOURCEOUTPUT: {
		uint32_t device = type == ENTRY_SINKINPUT
							  ? ((const pa_sink_input_info *)info)->sink
							  : ((const pa_source_output_info *)info)->source;
		if (data->device.index != device) {
			data->device.index = device;
			if (data->device.name != NULL) {
				free((void *)data->device.name);
				data->device.name = NULL;
			}
		}
		break;
	}
	case ENTRY_SINK: {
		data->ports.current = -1;
		for (size_t i = 0; i < data->ports.len; i++) {
			free((void *)data->ports.items[i].name);
			free((void *)data->ports.items[i].description);
		}
		data->ports.len = 0;
		const pa_sink_info *si = ((const pa_sink_info *)info);
		for (uint32_t i = 0; i < si->n_ports; i++) {
			NameDesc port = {
				.name = strdup(si->ports[i]->name),
				.description = strdup(si->ports[i]->description),
			};
			da_append(&data->ports, port);
			if (si->active_port == si->ports[i])
				data->ports.current = i;
		}
		break;
	}
	case ENTRY_SOURCE: {
		data->ports.current = -1;
		for (size_t i = 0; i < data->ports.len; i++) {
			free((void *)data->ports.items[i].name);
			free((void *)data->ports.items[i].description);
		}
		data->ports.len = 0;
		const pa_source_info *si = ((const pa_source_info *)info);
		for (uint32_t i = 0; i < si->n_ports; i++) {
			NameDesc port = {
				.name = strdup(si->ports[i]->name),
				.description = strdup(si->ports[i]->description),
			};
			da_append(&data->ports, port);
			if (si->active_port == si->ports[i])
				data->ports.current = i;
		}
		break;
	}
	case ENTRY_CARD: {
		data->profiles.current = -1;
		for (size_t i = 0; i < data->profiles.len; i++) {
			free((void *)data->profiles.items[i].name);
			free((void *)data->profiles.items[i].description);
		}
		data->profiles.len = 0;
		const pa_card_info *si = ((const pa_card_info *)info);
		for (uint32_t i = 0; i < si->n_profiles; i++) {
			NameDesc profile = {
				.name = strdup(si->profiles2[i]->name),
				.description = strdup(si->profiles2[i]->description),
			};
			da_append(&data->profiles, profile);
			if (si->active_profile2 == si->profiles2[i])
				data->ports.current = i;
		}
		break;
	}
	default:
		__builtin_unreachable();
	}
}

void app_entry_info(const void *info, entry_type type) {
	uint32_t index = pa_entry_index(info, type);
	const char *name = pa_entry_name(info, type);
	assert(info != NULL);
	pthread_mutex_lock(&app.mutex);
	int i = find_entry_with_index(index, type);
	if (i != -1) {
		Entry *entry = &app.entries.items[i];
		assert(entry->type == type);
		entry->marked = false;
		entry->volume = pa_entry_volume(info, type);
		entry->corked = pa_entry_corked(info, type);
		entry->channel_map = pa_entry_channel_map(info, type);
		entry->muted = pa_entry_mute(info, type);
		entry->monitor_index = pa_entry_monitor_index(info, type);
		if (entry->props != NULL) {
			pa_proplist_free(entry->props);
		}
		if (pa_entry_corked(info, type))
			entry->peak = 0;
		entry->props = pa_proplist_copy(pa_entry_proplist(info, type));
		apply_entry_data(&entry->data, info, type);
	} else {
		Entry ent = {
			.type = type,
			.name = strdup(name),
			.pa_index = index,
			.volume = pa_entry_volume(info, type),
			.channel_map = pa_entry_channel_map(info, type),
			.props = pa_proplist_copy(pa_entry_proplist(info, type)),
			.monitor_index = pa_entry_monitor_index(info, type),
			.muted = pa_entry_mute(info, type),
			.corked = pa_entry_corked(info, type),
			.volume_lock = type != ENTRY_CARD,
		};
		apply_entry_data(&ent.data, info, type);
		da_append(&app.entries, ent);
	}
	pthread_mutex_unlock(&app.mutex);
}
void app_sink_input_info(pa_context *ctx, const pa_sink_input_info *info, int eol, void *data) {
	(void)ctx;
	(void)data;
	if (info == NULL) {
		if (eol)
			pa_threaded_mainloop_signal(app.pa_mainloop, false);
		return;
	}
	app_entry_info(info, ENTRY_SINKINPUT);
}
void app_source_output_info(pa_context *ctx, const pa_source_output_info *info, int eol, void *data) {
	(void)ctx;
	(void)data;
	if (info == NULL) {
		if (eol)
			pa_threaded_mainloop_signal(app.pa_mainloop, false);
		return;
	}
	// hide peak-detection streams
	const char *appname = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_ID);
	if (appname != NULL && strcmp(appname, "org.PulseAudio.pavucontrol") == 0) {
		return;
	}
	app_entry_info(info, ENTRY_SOURCEOUTPUT);
}

void app_sink_info(pa_context *ctx, const pa_sink_info *info, int eol, void *data) {
	(void)ctx;
	(void)data;
	if (info == NULL) {
		if (eol)
			pa_threaded_mainloop_signal(app.pa_mainloop, false);
		return;
	}
	app_entry_info(info, ENTRY_SINK);
}

void app_source_info(pa_context *ctx, const pa_source_info *info, int eol, void *data) {
	(void)ctx;
	(void)data;
	if (info == NULL) {
		if (eol)
			pa_threaded_mainloop_signal(app.pa_mainloop, false);
		return;
	}
	// hide monitors
	const char *devtyp = pa_proplist_gets(info->proplist, PA_PROP_DEVICE_CLASS);
	if(devtyp != NULL && strcmp(devtyp, "monitor") == 0) {
		return;
	}
	app_entry_info(info, ENTRY_SOURCE);
}

void app_card_info(pa_context *ctx, const pa_card_info *info, int eol, void *data) {
	(void)ctx;
	(void)data;
	if (info == NULL) {
		if (eol)
			pa_threaded_mainloop_signal(app.pa_mainloop, false);
		return;
	}
	app_entry_info(info, ENTRY_CARD);
}

static void app_sink_info_name(pa_context *ctx, const pa_sink_info *i, int eol, void *userdata) {
	(void)ctx;
	if (eol) {
		assert(i == NULL);
		pa_threaded_mainloop_signal(app.pa_mainloop, false);
		return;
	}
	Entry *ent = userdata;
	ent->data.device.name = strdup(i->description);
}
static void app_source_info_name(pa_context *ctx, const pa_source_info *i, int eol, void *userdata) {
	(void)ctx;
	if (eol) {
		assert(i == NULL);
		pa_threaded_mainloop_signal(app.pa_mainloop, false);
		return;
	}
	Entry *ent = userdata;
	ent->data.device.name = strdup(i->description);
}

bool app_refresh_entries(App *app) {
	pa_threaded_mainloop_lock(app->pa_mainloop);
	pthread_mutex_lock(&app->mutex);
	if (app->pa_context == NULL || pa_context_get_state(app->pa_context) != PA_CONTEXT_READY) {
		pthread_mutex_unlock(&app->mutex);
		pa_threaded_mainloop_unlock(app->pa_mainloop);
		return false;
	}

	for (size_t i = 0; i < app->entries.len; i++)
		app->entries.items[i].marked = true;

	pa_operation *op;
	pa_operation_state_t state;

	switch (app->entry_page) {
	case ENTRY_SINKINPUT:
		op = pa_context_get_sink_input_info_list(app->pa_context, &app_sink_input_info, NULL);
		break;
	case ENTRY_SOURCEOUTPUT:
		op = pa_context_get_source_output_info_list(app->pa_context, &app_source_output_info, NULL);
		break;
	case ENTRY_SINK:
		op = pa_context_get_sink_info_list(app->pa_context, &app_sink_info, NULL);
		break;
	case ENTRY_SOURCE:
		op = pa_context_get_source_info_list(app->pa_context, &app_source_info, NULL);
		break;
	case ENTRY_CARD:
		op = pa_context_get_card_info_list(app->pa_context, &app_card_info, NULL);
		break;
	default:
		__builtin_unreachable();
	}
	assert(op != NULL);

	pthread_mutex_unlock(&app->mutex);
	while ((state = pa_operation_get_state(op)) == PA_OPERATION_RUNNING) {
		pa_threaded_mainloop_wait(app->pa_mainloop);
	}
	pa_operation_unref(op);
	if(state == PA_OPERATION_CANCELLED) {
		pa_threaded_mainloop_unlock(app->pa_mainloop);
		return false;
	}
	pthread_mutex_lock(&app->mutex);
	assert(state == PA_OPERATION_DONE);

	cull_entries(&app->entries);

	uint32_t indexbuf[app->entries.len];
	for(size_t i = 0; i < app->entries.len; i++)
		indexbuf[i] = app->entries.items[i].pa_index;
	struct entry_indices indices = {.count = (int)app->entries.len, .indices = indexbuf};
	qsort_r(app->entries.items, app->entries.len, sizeof(*app->entries.items), cmp_entry, &indices);

	for (size_t i = 0; i < app->entries.len; i++) {
		Entry *ent = &app->entries.items[i];
		// populate device name
		if ((ent->type == ENTRY_SINKINPUT || ent->type == ENTRY_SOURCEOUTPUT) && ent->data.device.name == NULL) {
			pa_operation *op;
			switch (ent->type) {
			case ENTRY_SINKINPUT:
				op = pa_context_get_sink_info_by_index(app->pa_context, ent->data.device.index, &app_sink_info_name, ent);
				break;
			case ENTRY_SOURCEOUTPUT:
				op = pa_context_get_source_info_by_index(app->pa_context, ent->data.device.index, &app_source_info_name, ent);
				break;
			default:
				__builtin_unreachable();
			}
			assert(op != NULL);
			pa_operation_state_t state;
			pthread_mutex_unlock(&app->mutex);
			while ((state = pa_operation_get_state(op)) == PA_OPERATION_RUNNING) {
				pa_threaded_mainloop_wait(app->pa_mainloop);
			}
			pa_operation_unref(op);
			if(state == PA_OPERATION_CANCELLED) {
				pa_threaded_mainloop_unlock(app->pa_mainloop);
				return false;
			}
			pthread_mutex_lock(&app->mutex);
			assert(state == PA_OPERATION_DONE);
			assert(ent->data.device.name != NULL);
		}

		// ensure monitor stream exists
		if (ent->monitor_stream != NULL || ent->type == ENTRY_CARD)
			continue;
		// we exclude corked entries, because those monitor streams will be stuck in creating state, which can't be
		// disconnected yet, so it just accumulates dead streams when switching tabs
		if (ent->type == ENTRY_SINKINPUT && !ent->corked) {
			ent->monitor_stream = create_monitor(app->pa_context, ent->pa_index, PA_INVALID_INDEX);
		}
		if (ent->type == ENTRY_SOURCEOUTPUT && !ent->corked) {
			const char *appname = pa_proplist_gets(ent->props, PA_PROP_APPLICATION_ID);
			if (appname == NULL || strcmp(appname, "org.PulseAudio.pavucontrol") != 0) {
				ent->monitor_stream = create_monitor(app->pa_context, PA_INVALID_INDEX, ent->monitor_index);
			}
		}
		if ((ent->type == ENTRY_SINK || ent->type == ENTRY_SOURCE)) {
			ent->monitor_stream = create_monitor(app->pa_context, PA_INVALID_INDEX, ent->monitor_index);
		}
	}

	pthread_mutex_unlock(&app->mutex);
	pa_threaded_mainloop_unlock(app->pa_mainloop);
	return true;
}
void app_init(App *app, pa_context *context, pa_threaded_mainloop *mainloop) {
	app->pa_context = context;
	app->pa_mainloop = mainloop;
	app->entry_page = ENTRY_SINKINPUT;
	app->running = true;
	app->resized = ATOMIC_VAR_INIT(false);
	int err = pthread_mutex_init(&app->mutex, NULL);
	if(err != 0) {
		fprintf(stderr, "failed to create pthread mutex: %d\n", err);
		exit(1);
	}
}

static void entry_data_free(Entry *entry) {
	switch(entry->type) {
		case ENTRY_SINKINPUT:
		case ENTRY_SOURCEOUTPUT:
			if(entry->data.device.name != NULL)
				free((void*)entry->data.device.name);
			break;
		case ENTRY_SINK:
		case ENTRY_SOURCE:
		case ENTRY_CARD:
			for(size_t i = 0; i < entry->data.ports.len; i++) {
				free((void*)entry->data.ports.items[i].name);
				free((void*)entry->data.ports.items[i].description);
			}
			entry->data.ports.len = 0;
			entry->data.ports.current = -1;
			if(entry->data.ports.items != 0) {
				free(entry->data.ports.items);
				entry->data.ports.items = NULL;
				entry->data.ports.cap = 0;
			}
			break;
	}
}
void entry_free(Entry *entry) {
	if(entry->name != NULL) {
		free((void*)entry->name);
		entry->name = NULL;
	}
	if(entry->props != NULL){
		pa_proplist_free(entry->props);
		entry->props = NULL;
	}
	entry_data_free(entry);
	if(entry->monitor_stream != NULL && pa_stream_get_state(entry->monitor_stream) == PA_STREAM_READY){
		int err = pa_stream_disconnect(entry->monitor_stream);
		assert(err == 0);
		pa_stream_unref(entry->monitor_stream);
		entry->monitor_stream = NULL;
	}
}
