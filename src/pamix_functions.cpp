#include <pamix_functions.hpp>

static pamix_ui *pamixUi;

inline bool isConnected() {
	return pamixUi->m_paInterface->isConnected();
}

void pamix_setup(pamix_ui *ui) {
	pamixUi = ui;
}

void pamix_quit(argument_t arg) {
	quit();
}

void pamix_select_tab(argument_t arg) {
	pamixUi->selectEntries(static_cast<entry_type>(arg.i));
	signal_update(true);
}

void pamix_select_next(argument_t arg) {
	pamixUi->selectNext(arg.b);
	signal_update(true);
}

void pamix_select_prev(argument_t arg) {
	pamixUi->selectPrevious(arg.b);
	signal_update(true);
}

void pamix_set_volume(argument_t arg) {
	set_volume(pamixUi, arg.d);
}

void pamix_add_volume(argument_t arg) {
	add_volume(pamixUi, arg.d);
}

void pamix_cycle_next(argument_t arg) {
	cycle_switch(pamixUi, true);
}

void pamix_cycle_prev(argument_t arg) {
	cycle_switch(pamixUi, false);
}

void pamix_toggle_lock(argument_t arg) {
	toggle_lock(pamixUi);
	signal_update(true);
}

void pamix_set_lock(argument_t arg) {
	set_lock(pamixUi, arg.b);
	signal_update(true);
}

void pamix_toggle_mute(argument_t arg) {
	toggle_mute(pamixUi);
	signal_update(true);
}

void pamix_set_mute(argument_t arg) {
	set_mute(pamixUi, arg.b);
	signal_update(true);
}
