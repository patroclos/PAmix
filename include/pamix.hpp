#pragma once

#include <painterface.hpp>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

struct UpdateData {
	bool redrawAll;

	UpdateData() = default;

	explicit UpdateData(bool redrawAll) { this->redrawAll = redrawAll; }
};

#define DECAY_STEP 0.04
#define MAX_VOL 1.5

#ifdef FEAT_UNICODE
#define SYM_VOLBAR L'\u25ae' //▮
#define SYM_ARROW "\u25b6 " //▶
#define SYM_MUTE "🔇"
#define SYM_LOCK "🔒"
#define SYM_SPACE L' '
#define FEAT_UNICODE_STRING std::wstring
#define FEAT_UNICODE_MVADDNSTR(y, x, str, n) mvaddnwstr(y, x, str, n);
#else
#define SYM_VOLBAR '|'
#define SYM_ARROW "> "
#define SYM_MUTE "M"
#define SYM_LOCK "L"
#define SYM_SPACE ' '
#define FEAT_UNICODE_STRING std::string
#define FEAT_UNICODE_MVADDNSTR(y, x, str, n) mvaddnstr(y, x, str, n);
#endif

void quit();

void signal_update(bool all, bool threaded = false);

void selectEntries(PAInterface *interface, entry_type type);

void set_volume(PAInterface *interface, double pct);

void add_volume(PAInterface *interface, double pct);

void cycle_switch(PAInterface *interface, bool inc);

void set_mute(PAInterface *interface, bool mute);

void toggle_mute(PAInterface *interface);

void set_lock(PAInterface *interface, bool lock);

void toggle_lock(PAInterface *interface);

void select_next(PAInterface *interface, bool precise);

void select_previous(PAInterface *interface, bool precise);
