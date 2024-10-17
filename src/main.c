#include <assert.h>
#include <locale.h>
#include <ncurses.h>
#include <pthread.h>
#include <pulse/pulseaudio.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "app.h"
#include "da.h"
#include "draw.h"
#include "config.h"

struct line_expect {
	int begin;
	int *end;
	int expected;
};

void line_expect_check(struct line_expect *e) {
	int count = (*e->end) - e->begin;
	assert(e->expected == count);
}
static inline int expected_entry_lines(const Entry *ent) {
	int channel_lines = ent->type == ENTRY_CARD ? 0: (ent->volume_lock ? 1 : ent->volume.channels);
	return channel_lines + 1 + (ent->type != ENTRY_CARD);
}

int compute_entry_scroll();

void on_ctx_state(pa_context *ctx, void *data) {
	(void)ctx;
	pa_threaded_mainloop *mainloop = (pa_threaded_mainloop *)data;
	pa_threaded_mainloop_signal(mainloop, false);
}

void on_ctx_subscription(pa_context *ctx, pa_subscription_event_type_t evt_type, uint32_t index, void *data) {
	(void)ctx;
	(void)evt_type;
	(void)index;
	(void)data;
	atomic_store(&app.should_refresh, true);
	pa_threaded_mainloop_signal(app.pa_mainloop, false);
}

void cb_success_signal(pa_context *ctx, int succ, void *data) {
	(void)ctx;
	(void)succ;
	(void)data;
	pa_threaded_mainloop_signal(app.pa_mainloop, false);
}

void on_signal_resize(int signal) {
	(void)signal;
	atomic_store(&app.resized, true);
	pa_threaded_mainloop_signal(app.pa_mainloop, false);
}

void *input_thread_main(void *data) {
	(void)data;
	while (app.running) {
		pthread_mutex_lock(&app.mutex);
		int ch = getch();
		pthread_mutex_unlock(&app.mutex);
#ifdef KEY_RESIZE
		if (ch == KEY_RESIZE) {
			atomic_store(&app.resized, true);
		}
#endif

		bool key_valid = ch != ERR && ch != KEY_RESIZE && ch != KEY_MOUSE;
		if (key_valid) {
			InputEvent evt = {
				.keycode = ch,
				.keyname = keyname(ch),
			};
			assert(evt.keyname != NULL);
			pthread_mutex_lock(&app.mutex);
			da_append(&app.input_queue, evt);
			pthread_mutex_unlock(&app.mutex);
		}
		if (key_valid || ch == KEY_RESIZE) {
			pa_threaded_mainloop_signal(app.pa_mainloop, false);
		}
		usleep(2000);
	}
	return NULL;
}

void *reconnect_thread_main(void *arg) {
	(void)arg;
	while (app.running) {
		pa_proplist *props;
		pa_mainloop_api *api;
		int err;
		pa_threaded_mainloop_lock(app.pa_mainloop);
		pthread_mutex_lock(&app.mutex);
		if (app.pa_context != NULL && pa_context_get_state(app.pa_context) == PA_CONTEXT_READY) {
			goto sleep;
		}
		if (app.pa_context != NULL) {
			pa_context_unref(app.pa_context);
			app.pa_context = NULL;
		}

		props = pa_proplist_new();
		pa_proplist_sets(props, PA_PROP_APPLICATION_ID, "testerino");
		pa_proplist_sets(props, PA_PROP_APPLICATION_NAME, "testerino");
		api = pa_threaded_mainloop_get_api(app.pa_mainloop);
		app.pa_context = pa_context_new_with_proplist(api, "testerino", props);
		pa_proplist_free(props);
		pa_context_set_state_callback(app.pa_context, &on_ctx_state, app.pa_mainloop);
		err = pa_context_connect(app.pa_context, NULL, (pa_context_flags_t)PA_CONTEXT_NOAUTOSPAWN, NULL);
		if (err != 0) {
			pa_context_unref(app.pa_context);
			app.pa_context = NULL;
			// TODO: on error we probably want to sleep with the lock held, so noone else does any funny business
			goto sleep;
		}
		pa_context_state_t state;
		pthread_mutex_unlock(&app.mutex);
		while((state = pa_context_get_state(app.pa_context)) != PA_CONTEXT_READY){
			assert(PA_CONTEXT_IS_GOOD(state));
			pa_threaded_mainloop_wait(app.pa_mainloop);
		}
		pthread_mutex_lock(&app.mutex);
		{
			pa_context_set_subscribe_callback(app.pa_context, &on_ctx_subscription, NULL);
			pa_subscription_mask_t submask = PA_SUBSCRIPTION_MASK_ALL;
			pa_operation *op = pa_context_subscribe(app.pa_context, submask, &cb_success_signal, app.pa_mainloop);

			pa_operation_state_t opstate;
			pthread_mutex_unlock(&app.mutex);
			while ((opstate = pa_operation_get_state(op)) == PA_OPERATION_RUNNING) {
				pa_threaded_mainloop_wait(app.pa_mainloop);
			}
			pthread_mutex_lock(&app.mutex);
			pa_operation_unref(op);
			if(opstate != PA_OPERATION_DONE)
				goto sleep;
		}

		atomic_store(&app.should_refresh, true);
		pa_threaded_mainloop_signal(app.pa_mainloop, false);

	sleep:
		pthread_mutex_unlock(&app.mutex);
		pa_threaded_mainloop_unlock(app.pa_mainloop);
		sleep(5);
	}
	return NULL;
}

pa_operation *entry_set_volume(Entry ent, const pa_cvolume *volume) {
#define OP(name) pa_context_set_##name(app.pa_context, ent.pa_index, volume, &cb_success_signal, NULL)
	switch (ent.type) {
	case ENTRY_SINKINPUT:
		return OP(sink_input_volume);
	case ENTRY_SOURCEOUTPUT:
		return OP(source_output_volume);
	case ENTRY_SINK:
		return OP(sink_volume_by_index);
	case ENTRY_SOURCE:
		return OP(source_volume_by_index);
	case ENTRY_CARD:
		return NULL;
	}
	__builtin_unreachable();
#undef OP
}

pa_operation *entry_set_muted(Entry ent, bool mute) {
#define OP(name) pa_context_set_##name(app.pa_context, ent.pa_index, mute, &cb_success_signal, NULL)
	switch (ent.type) {
	case ENTRY_SINKINPUT:
		return OP(sink_input_mute);
	case ENTRY_SOURCEOUTPUT:
		return OP(source_output_mute);
	case ENTRY_SINK:
		return OP(sink_mute_by_index);
	case ENTRY_SOURCE:
		return OP(source_mute_by_index);
	case ENTRY_CARD:
		return NULL;
	}
	__builtin_unreachable();
#undef OP
}

void collect_sink_indices(pa_context *ctx, const pa_sink_info *i, int eol, void *userdata) {
	(void)ctx;
	if (eol) {
		assert(i == NULL);
		pa_threaded_mainloop_signal(app.pa_mainloop, false);
		return;
	}
	pthread_mutex_lock(&app.mutex);
	uint32_t **ptr = (uint32_t **)userdata;
	*((*ptr)++) = i->index;
	pthread_mutex_unlock(&app.mutex);
}

void collect_source_indices(pa_context *ctx, const pa_source_info *i, int eol, void *userdata) {
	(void)ctx;
	if (eol) {
		assert(i == NULL);
		pa_threaded_mainloop_signal(app.pa_mainloop, false);
		return;
	}
	pthread_mutex_lock(&app.mutex);
	uint32_t **ptr = (uint32_t **)userdata;
	*((*ptr)++) = i->index;
	pthread_mutex_unlock(&app.mutex);
}

#define RUN_OPERATION_OR_RETURN(operation, ostate, or_return) \
	do { \
		assert(operation != NULL); \
		pthread_mutex_unlock(&app.mutex); \
		while(((ostate) = pa_operation_get_state(operation)) == PA_OPERATION_RUNNING) \
			pa_threaded_mainloop_wait(app.pa_mainloop); \
		pthread_mutex_lock(&app.mutex); \
		pa_operation_unref(operation); \
		if((ostate) == PA_OPERATION_CANCELLED) { \
			return or_return; \
		}\
		assert((ostate) == PA_OPERATION_DONE);\
	} while(0)

// caller should hold mainloop and app-mutex
// return false on failure
static bool drain_input_queue(const Config *cfg) {
	for (size_t i = 0; i < app.input_queue.len; i++) {
		InputEvent evt = app.input_queue.items[i];
		Action act = cfg->keymap[evt.keycode];
		if (act.type == ACTION_QUIT) {
			app.running = false;
			continue;
		}
		if (act.type == ACTION_SELECT_TAB) {
			app.entry_page = cfg->keymap[evt.keycode].data.tab;
			app.selected_entry = 0;
			app.selected_channel = 0;
			atomic_store(&app.should_refresh, true);
			continue;
		}
		if (act.type == ACTION_DEVICE_NEXT || act.type == ACTION_DEVICE_PREV) {
			Entry ent = app.entries.items[app.selected_entry];
			int off = act.type == ACTION_DEVICE_NEXT ? 1 : -1;

			switch (ent.type) {
			case ENTRY_SINKINPUT:
			case ENTRY_SOURCEOUTPUT: {
				if (ent.data.device.index == PA_INVALID_INDEX)
					break;
				uint32_t device_list[512] = {0};
				for (size_t i = 0; i < sizeof(device_list) / sizeof(*device_list); i++)
					device_list[i] = PA_INVALID_INDEX;

				uint32_t *end = device_list;
				pa_operation *op;
				pa_operation_state_t state;
				if (ent.type == ENTRY_SINKINPUT)
					op = pa_context_get_sink_info_list(app.pa_context, &collect_sink_indices, &end);
				else
					op = pa_context_get_source_info_list(app.pa_context, &collect_source_indices, &end);

				assert(op != NULL);

				RUN_OPERATION_OR_RETURN(op, state, false);
				int device_count = (uintptr_t)(end - device_list);
				assert((size_t)device_count <= sizeof(device_list) / sizeof(*device_list));

				int current_index = -1;
				for (int i = 0; i < device_count; i++) {
					if (ent.data.device.index != device_list[i])
						continue;
					current_index = i;
					break;
				}
				assert(current_index >= 0);

				int idev = (current_index + off) % device_count;
				if(idev == -1)
					idev = device_count - 1;
				assert(idev >= 0);
				assert(idev < device_count);
				uint32_t new_device = device_list[idev];
				if (ent.type == ENTRY_SINKINPUT)
					op = pa_context_move_sink_input_by_index(app.pa_context, ent.pa_index, new_device, &cb_success_signal, NULL);
				else
					op = pa_context_move_source_output_by_index(app.pa_context, ent.pa_index, new_device, &cb_success_signal, NULL);
				assert(op != NULL);
				RUN_OPERATION_OR_RETURN(op, state, false);
				break;
			}
			case ENTRY_SINK:
			case ENTRY_SOURCE:
			case ENTRY_CARD: {
				if (ent.data.ports.current == -1)
					break;
				assert((int)ent.data.ports.len > ent.data.ports.current);
				int next = ((ent.data.ports.current + off) % ent.data.ports.len);
				if (next == ent.data.ports.current) {
					assert(ent.data.ports.len == 1);
					break;
				}
				const char *name = ent.data.ports.items[next].name;

				pa_operation *op;
				pa_operation_state_t state;
				if (ent.type == ENTRY_SINK)
					op = pa_context_set_sink_port_by_name(app.pa_context, ent.name, name, &cb_success_signal, NULL);
				else if (ent.type == ENTRY_SOURCE)
					op = pa_context_set_source_port_by_name(app.pa_context, ent.name, name, &cb_success_signal, NULL);
				else
					op = pa_context_set_card_profile_by_name(app.pa_context, ent.name, name, &cb_success_signal, NULL);
				assert(op != NULL);

				RUN_OPERATION_OR_RETURN(op, state, false);
			}
			}
			continue;
		}
		if (act.type == ACTION_ENTRY_NEXT || act.type == ACTION_ENTRY_PREV) {
			int off = act.type == ACTION_ENTRY_NEXT ? 1 : -1;
			bool entry_bounds = app.selected_entry + off < 0 || app.selected_entry + off >= (int)app.entries.len;
			Entry ent = app.entries.items[app.selected_entry];
			if (ent.volume_lock && !entry_bounds) {
				app.selected_entry += off;
				Entry other = app.entries.items[app.selected_entry];
				if (other.volume_lock)
					app.selected_channel = 0;
				else if (off < 0)
					app.selected_channel = other.volume.channels - 1;
			} else {
				if (off > 0 && ent.volume.channels > app.selected_channel + off) {
					app.selected_channel += off;
				} else if (off < 0 && app.selected_channel > 0) {
					app.selected_channel += off;
				} else if (!entry_bounds) {
					app.selected_entry += off;
					Entry other = app.entries.items[app.selected_entry];
					if (other.volume_lock)
						app.selected_channel = 0;
					else if (off < 0)
						app.selected_channel = other.volume.channels - 1;
				}
			}
			atomic_store(&app.should_refresh, true);
			continue;
		}
		if (act.type == ACTION_LOCK_TOGGLE) {
			Entry *ent = &app.entries.items[app.selected_entry];
			if (ent->volume.channels == 0)
				continue;
			ent->volume_lock = !ent->volume_lock;
			app.selected_channel = 0;
			atomic_store(&app.should_refresh, true);
			continue;
		}
		if (act.type == ACTION_MUTE_TOGGLE) {
			Entry ent = app.entries.items[app.selected_entry];
			pa_operation *op = entry_set_muted(ent, !ent.muted);
			if (op == NULL)
				continue;
			pa_operation_state_t state;
			RUN_OPERATION_OR_RETURN(op, state, false);
			continue;
		}
		if (act.type == ACTION_VOLUME_SET || act.type == ACTION_VOLUME_ADD) {
			Entry ent = app.entries.items[app.selected_entry];
			if (ent.volume.channels == 0)
				continue;
			pa_volume_t newvol;
			if (act.type == ACTION_VOLUME_SET) {
				if (ent.volume_lock) {
					newvol = (pa_volume_t)((float)PA_VOLUME_NORM * act.data.volume);
					pa_cvolume_set(&ent.volume, ent.volume.channels, newvol);
				} else {
					assert(app.selected_channel >= 0 && app.selected_channel < ent.volume.channels);
					ent.volume.values[app.selected_channel] = PA_VOLUME_NORM * act.data.volume;
				}
			} else {
				if (ent.volume_lock) {
					newvol = pa_cvolume_avg(&ent.volume) + PA_VOLUME_NORM * act.data.volume;
					pa_cvolume_set(&ent.volume, ent.volume.channels, newvol);
				} else {
					assert(app.selected_channel >= 0 && app.selected_channel < ent.volume.channels);
					ent.volume.values[app.selected_channel] += PA_VOLUME_NORM * act.data.volume;
				}
			}

			pa_operation *op = entry_set_volume(ent, &ent.volume);
			assert(op != NULL);
			pa_operation_state_t state;
			RUN_OPERATION_OR_RETURN(op, state, false);
			atomic_store(&app.should_refresh, true);
			continue;
		}
	}
	app.input_queue.len = 0;
	return true;
}

int main() {
	Config cfg = {0};
	do {
		const char *home = getenv("HOME");
		const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
		const char *xdg_config_dirs = getenv("XDG_CONFIG_DIRS");
		
		char config_path[PATH_MAX];
		if(xdg_config_home != NULL)
			snprintf(config_path, PATH_MAX - 1, "%s/pamix.conf", xdg_config_home);
		else
			snprintf(config_path, PATH_MAX - 1, "%s/.config/pamix.conf", home);

		if(config_load(&cfg, config_path) == 0)
			break;

		if(xdg_config_dirs == NULL)
			xdg_config_dirs = "/etc/xdg";

		snprintf(config_path, PATH_MAX - 1, "%s/pamix.conf", xdg_config_dirs);
		if(config_load(&cfg, config_path) == 0)
			break;

		config_default(&cfg);
	} while(0);

	pa_threaded_mainloop *mainloop = pa_threaded_mainloop_new();
	assert(mainloop != NULL);

	pa_threaded_mainloop_lock(mainloop);
	if (pa_threaded_mainloop_start(mainloop) == -1) {
		fprintf(stderr, "could not start mainloop\n");
		return 1;
	}

	pa_threaded_mainloop_unlock(mainloop);

	// we pass NULL as pa_context* and let the reconnect thread handle it
	app_init(&app, NULL, mainloop);
	atomic_store(&app.should_refresh, true);
	app.entry_page = ENTRY_SINKINPUT;

	{
		setlocale(LC_ALL, "");
		initscr();
		nodelay(stdscr, true);
		set_escdelay(25);
		curs_set(0);
		keypad(stdscr, true);
		meta(stdscr, true);
		noecho();

		start_color();
		int background = use_default_colors() ? 0 : -1;
		init_pair(1, COLOR_GREEN, background);
		init_pair(2, COLOR_YELLOW, background);
		init_pair(3, COLOR_RED, background);
	}

	signal(SIGWINCH, on_signal_resize);

	struct EntLine {
		uint32_t entry;
		uint32_t line;
	};
	struct EntLines {
		struct EntLine *items;
		size_t len;
		size_t cap;
	};
	struct EntLines entry_lines = {0};

	pthread_t input_thread;
	pthread_t reconnect_thread;
	int pthread_status = pthread_create(&input_thread, NULL, &input_thread_main, NULL);
	pthread_status |= pthread_create(&reconnect_thread, NULL, &reconnect_thread_main, NULL);
	if(pthread_status != 0) {
		fprintf(stderr, "failed to create threads\n");
		exit(1);
	}

	while (app.running) {
		{
			pa_threaded_mainloop_lock(mainloop);
			pthread_mutex_lock(&app.mutex);

			if(app.pa_context == NULL || pa_context_get_state(app.pa_context) != PA_CONTEXT_READY) {
				erase();
				mvprintw(0, 0, "Waiting for PulseAudio connection...");
				refresh();
				app.input_queue.len = 0;
				pthread_mutex_unlock(&app.mutex);
				pa_threaded_mainloop_wait(app.pa_mainloop);
				pa_threaded_mainloop_unlock(app.pa_mainloop);
				continue;
			}

			bool ok = drain_input_queue(&cfg);
			if(!ok) {
				pthread_mutex_unlock(&app.mutex);
				pa_threaded_mainloop_unlock(app.pa_mainloop);
				continue;
			}

			pthread_mutex_unlock(&app.mutex);
			pa_threaded_mainloop_unlock(mainloop);
		}
		if (atomic_exchange(&app.resized, false)) {
			pthread_mutex_lock(&app.mutex);
			endwin();
			refresh();
			pthread_mutex_unlock(&app.mutex);
			atomic_store(&app.should_refresh, true);
		}
		if (atomic_exchange(&app.should_refresh, false)) {
			bool ok = app_refresh_entries(&app);
			if(!ok)
				continue;
			pthread_mutex_lock(&app.mutex);
			app.scroll = compute_entry_scroll();
			erase();
			app.should_refresh = false;
			app.new_peaks = false;


			const char *entry_type_names[] = {"Playback", "Recording", "Output Devices", "Input Devices", "Cards"};
			move(0, 1);
			printw("%d/%zu", app.selected_entry + 1, app.entries.len);
			mvaddstr(0, 10, entry_type_names[app.entry_page]);

			int line = 1;
			entry_lines.len = 0;
			for (size_t i = app.scroll; i < app.entries.len; i++) {
				line++;
				Entry *ent = &app.entries.items[i];

				bool selected = app.selected_entry == (int)i;
				int entsize = 1;
				if (line + entsize + 2 > LINES) {
					assert(!selected || (int)i == app.scroll);
					break;
				}

				struct line_expect __attribute__((cleanup(line_expect_check))) expect = {
					.begin = line,
					.end = &line,
					.expected = expected_entry_lines(ent),
				};

				int width = COLS - 33;
				int x = 32;
				// volume control bars
				if (ent->volume_lock && ent->volume.channels > 0) {
					move(line, 1);
					if (app.selected_entry == (int)i) {
						addstr(">");
					}
					pa_volume_t vol = pa_cvolume_avg(&ent->volume);
					char buf[30];
					pa_sw_volume_snprint_dB(buf, sizeof(buf) - 1, vol);
					double pct = vol / (double)PA_VOLUME_NORM;
					addstr(buf);
					printw(" (%.2lf)", pct);
					draw_volume_bar(line++, x, width, vol);
				} else {
					for (uint8_t j = 0; j < ent->volume.channels; j++) {
						if (app.selected_entry == (int)i && app.selected_channel == j) {
							mvaddstr(line, 1, ">");
						}
						const char *channel_name = pa_channel_position_to_pretty_string(ent->channel_map.map[j]);
						mvaddstr(line, 3, channel_name);
						draw_volume_bar(line++, x, width, ent->volume.values[j]);
					}
				}

				// peak volume bar
				if(ent->type != ENTRY_CARD) {
					pa_volume_t peak = ent->peak * PA_VOLUME_NORM;
					if (ent->monitor_stream == NULL)
						peak = PA_VOLUME_MUTED;
					struct EntLine el = {.entry = ent->pa_index, .line = (uint32_t)line};
					da_append(&entry_lines, el);
					draw_volume_bar(line++, 1, COLS - 2, peak);
				}

				// entry name
				if (selected)
					attron(A_STANDOUT);
				switch (ent->type) {
				case ENTRY_SINKINPUT:
					mvaddstr(line, 1, pa_proplist_gets(ent->props, PA_PROP_APPLICATION_NAME));
					break;
				case ENTRY_SINK:
				case ENTRY_SOURCE:
					mvaddstr(line, 1, pa_proplist_gets(ent->props, PA_PROP_DEVICE_DESCRIPTION));
					printw(" %s", pa_proplist_gets(ent->props, PA_PROP_DEVICE_PROFILE_DESCRIPTION));
					break;
				case ENTRY_CARD:
					mvaddstr(line, 1, pa_proplist_gets(ent->props, PA_PROP_DEVICE_DESCRIPTION));
					break;
				default:
					mvaddstr(line, 1, ent->name);
					break;
				}
				attroff(A_STANDOUT);
				if (ent->volume_lock)
					printw(" 🔒");
				if (ent->muted)
					printw(" 🔇");
				if (ent->corked)
					printw(" ⏸");

				// device/port/profile display
				switch (ent->type) {
				case ENTRY_SINKINPUT:
				case ENTRY_SOURCEOUTPUT: {
					char buf[256];
					int dev_len = 0;
					assert(ent->data.device.name != NULL);
					if (ent->data.device.name != NULL)
						dev_len = snprintf(buf, sizeof(buf) - 1, "%s", ent->data.device.name);

					int name_len = strlen(ent->name);
					int max_name = COLS - 1 - dev_len - 4;
					
					x = getcurx(stdscr);
					if(x < max_name) {
						// TODO: color
						attron(A_DIM);
						if(name_len > max_name - x) {
							printw("  %.*s...", max_name - x - 3, ent->name);
						} else {
							printw("  %s", ent->name);
						}
						attroff(A_DIM);
					}

					mvaddstr(line, COLS - 1 - dev_len, buf);
					break;
				}
				case ENTRY_CARD:
				case ENTRY_SINK:
				case ENTRY_SOURCE: {
					char buf[256];
					int len;
					len = sprintf(buf, "%s", ent->data.ports.items[ent->data.ports.current].description);
					mvaddstr(line, COLS - 1 - len, buf);
					break;
				}
				default:
					break;
				}
				line++;
			}
			refresh();
			pthread_mutex_unlock(&app.mutex);
		} else if (app.new_peaks) {
			pthread_mutex_lock(&app.mutex);
			app.new_peaks = false;
			for (size_t i = 0; i < entry_lines.len; i++) {
				struct EntLine el = entry_lines.items[i];
				Entry *ent = NULL;
				for (size_t j = 0; j < app.entries.len; j++) {
					Entry *e = &app.entries.items[j];
					if (e->pa_index == el.entry) {
						ent = e;
						break;
					}
				}
				assert(ent != NULL);
				pa_volume_t peak = ent->peak * PA_VOLUME_NORM;
				if (ent->monitor_stream == NULL)
					peak = PA_VOLUME_MUTED;
				draw_volume_bar(el.line, 1, COLS - 2, peak);
			}
			refresh();
			pthread_mutex_unlock(&app.mutex);
		}

		if (!app.running)
			break;
		pa_threaded_mainloop_lock(mainloop);
		if (atomic_load(&app.should_refresh) || app.new_peaks || app.input_queue.len > 0) {
			pa_threaded_mainloop_unlock(mainloop);
			continue;
		}
		pa_threaded_mainloop_wait(app.pa_mainloop);
		pa_threaded_mainloop_unlock(mainloop);
	}

	pthread_join(input_thread, NULL);
	pthread_cancel(reconnect_thread);
	pthread_join(reconnect_thread, NULL);

	if(entry_lines.cap != 0)
		free(entry_lines.items);
	if(app.pa_context != NULL && PA_CONTEXT_IS_GOOD(pa_context_get_state(app.pa_context))){
		pa_context_disconnect(app.pa_context);
		pa_threaded_mainloop_stop(app.pa_mainloop);
		pa_threaded_mainloop_free(app.pa_mainloop);
	}
	for(size_t i = 0; i < app.entries.len; i++) {
		entry_free(&app.entries.items[i]);
	}

	endwin();
	return 0;
}


// compute new scroll so selected entry stays in view
int compute_entry_scroll() {
	int scroll = app.scroll;
	if (scroll > app.selected_entry)
		return app.selected_entry;

	int entry_sizes[app.entries.len];
	for (size_t i = 0; i < app.entries.len; i++) {
		entry_sizes[i] = expected_entry_lines(&app.entries.items[i]);
	}

	int line = 2;
	for (size_t i = scroll; i < app.entries.len; i++) {
		line += entry_sizes[i] + 1;
		if ((int)i < scroll || app.selected_entry != (int)i)
			continue;
		if (line > LINES) {
			int backscroll = 0;
			size_t j = scroll;
			for (; j < i && line - backscroll > LINES; j++)
				backscroll += entry_sizes[j];
			scroll = (int)j;
		}
		break;
	}

	return scroll;
}
