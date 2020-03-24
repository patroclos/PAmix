#include <pamix_functions.hpp>
#include <regex>

static pamix_ui *pamixUi;
static std::vector<entry_type> cycleOrder;

std::vector<entry_type> tabcycle_order_from_config(Configuration *config) {
	std::vector<entry_type> vec_order;

	bool hasOrder = config->has(CONFIGURATION_CYCLE_ORDER);
	if(!hasOrder)
		return vec_order;
	std::string value = config->getString(CONFIGURATION_CYCLE_ORDER);

	std::regex orderPattern("(\\d+)(?:,?\\s*)");

	std::sregex_iterator iter(value.begin(), value.end(), orderPattern);
	std::sregex_iterator end = std::sregex_iterator();

	for(std::sregex_iterator i = iter; i != end; ++i)
	{
		std::smatch match = *i;
		int result = std::atoi(match[1].str().c_str());
		if(result >= ENTRY_COUNT) {
			fprintf(stderr, "Configuration variable '%s' contains invalid tab id: %d\n", CONFIGURATION_CYCLE_ORDER, result);
			exit(1);
		}
		vec_order.push_back(static_cast<entry_type>(result));
	}

	return vec_order;
}

inline bool isConnected() {
	return pamixUi->m_paInterface->isConnected();
}

void pamix_setup(pamix_ui *ui) {
	pamixUi = ui;
	cycleOrder = tabcycle_order_from_config(ui->m_Config);
}

void pamix_quit(argument_t arg) {
	quit();
}

void pamix_select_tab(argument_t arg) {
	pamixUi->selectEntries(static_cast<entry_type>(arg.i));
	signal_update(true);
}

size_t current_cycle_index() {
	std::vector<entry_type>::iterator it = std::find(cycleOrder.begin(), cycleOrder.end(), pamixUi->m_EntriesType);
	return it != cycleOrder.end()
		? std::distance(cycleOrder.begin(), it)
		: 0;
}

void pamix_cycle_tab_next(argument_t arg) {
	size_t currentIndex = current_cycle_index();
	entry_type nextType = cycleOrder[(currentIndex + 1) % cycleOrder.size()];
	pamixUi->selectEntries(nextType);
	signal_update(true);
}

void pamix_cycle_tab_prev(argument_t arg) {
	size_t currentIndex = current_cycle_index();
	entry_type nextType = cycleOrder[(cycleOrder.size() + currentIndex - 1) % cycleOrder.size()];
	pamixUi->selectEntries(nextType);
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
