#pragma once

#include <../config.hpp>
#include <painterface.hpp>
#include "pamix_ui.hpp"

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

void set_volume(pamix_ui *ui, double pct);

void add_volume(pamix_ui *ui, double pct);

void cycle_switch(pamix_ui *ui, bool inc);

void set_mute(pamix_ui *ui, bool mute);

void toggle_mute(pamix_ui *ui);

void set_lock(pamix_ui *ui, bool lock);

void toggle_lock(pamix_ui *ui);

void select_next(PAInterface *interface, bool precise);

void select_previous(PAInterface *interface, bool precise);
