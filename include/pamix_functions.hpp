#pragma once

#include <pamix.hpp>
#include <string>
#include "pamix_ui.hpp"

union argument_t {
	double d;
	int    i;
	bool   b;
};

void pamix_setup(pamix_ui *interface);

void pamix_quit(argument_t arg);

void pamix_select_tab(argument_t arg);

void pamix_cycle_tab_next(argument_t arg);

void pamix_cycle_tab_prev(argument_t arg);

void pamix_select_next(argument_t arg);

void pamix_select_prev(argument_t arg);

void pamix_set_volume(argument_t arg);

void pamix_add_volume(argument_t arg);

void pamix_cycle_next(argument_t arg);

void pamix_cycle_prev(argument_t arg);

void pamix_toggle_lock(argument_t arg);

void pamix_set_lock(argument_t arg);

void pamix_toggle_mute(argument_t arg);

void pamix_set_mute(argument_t arg);
