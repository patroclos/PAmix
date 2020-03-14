#include <pamix.hpp>

#include <unistd.h>
#include <configuration.hpp>
#include <condition_variable>
#include <queue>
#include <csignal>

// GLOBAL VARIABLES
Configuration configuration;

bool running = true;

// sync main and callback threads
std::mutex updMutex;
std::condition_variable cv;

std::queue<UpdateData> updateDataQ;

void quit() {
	running = false;
	signal_update(false);
}

void __signal_update(bool all) {
	{
		std::lock_guard<std::mutex> lk(updMutex);
		updateDataQ.push(UpdateData(all));
	}
	cv.notify_one();
}

void signal_update(bool all, bool threaded) {
	if (threaded)
		std::thread(__signal_update, all).detach();
	else
		__signal_update(all);
}

void set_volume(pamix_ui *ui, double pct) {
	auto it = ui->getSelectedEntryIterator();
	if (it != ui->m_Entries->end())
		it->second->setVolume(it->second->m_Lock ? -1 : (int) ui->m_SelectedChannel, (pa_volume_t) (PA_VOLUME_NORM * pct));
}

void add_volume(pamix_ui *ui, double pct) {
	auto it = ui->getSelectedEntryIterator();
	if (it != ui->m_Entries->end())
		it->second->addVolume(it->second->m_Lock ? -1 : (int) ui->m_SelectedChannel, pct);
}

void cycle_switch(pamix_ui *ui, bool increment) {
	auto it = ui->getSelectedEntryIterator();
	if (it != ui->m_Entries->end())
		it->second->cycleSwitch(increment);
}

void set_mute(pamix_ui *ui, bool mute) {
	auto it = ui->getSelectedEntryIterator();
	if (it != ui->m_Entries->end())
		it->second->setMute(mute);
}

void toggle_mute(pamix_ui *ui) {
	auto it = ui->getSelectedEntryIterator();
	if (it != ui->m_Entries->end())
		it->second->setMute(!it->second->m_Mute);
}

void set_lock(pamix_ui *ui, bool lock) {
	auto it = ui->getSelectedEntryIterator();
	if (it != ui->m_Entries->end())
		it->second->m_Lock = lock;
}

void toggle_lock(pamix_ui *ui) {
	auto it = ui->getSelectedEntryIterator();
	if (it != ui->m_Entries->end())
		it->second->m_Lock = !it->second->m_Lock;
}

void inputThread(pamix_ui *ui) {
	while (running) {
		int ch = ui->getKeyInput();

		bool isValidKey = ch != ERR && ch != KEY_RESIZE && ch != KEY_MOUSE;
		if (isValidKey) {
			configuration.pressKey(ch);
		}
		usleep(2000);
	}
}

void pai_subscription(PAInterface *, pai_subscription_type_t type) {
	bool updAll = (type & PAI_SUBSCRIPTION_MASK_INFO) != 0
	              || (type & PAI_SUBSCRIPTION_MASK_CONNECTION_STATUS) != 0;
	signal_update(updAll);
}

void sig_handle_resize(int) {
	endwin();
	refresh();
	signal_update(true, true);
}

void init_colors() {
	start_color();
	init_pair(1, COLOR_GREEN, 0);
	init_pair(2, COLOR_YELLOW, 0);
	init_pair(3, COLOR_RED, 0);
}

void sig_handle(int) {
	endwin();
}

void loadConfiguration() {
	char *home = getenv("HOME");
	char *xdg_config_home = getenv("XDG_CONFIG_HOME");

	std::string path;

	path = xdg_config_home ? xdg_config_home : std::string(home) += "/.config";
	path += "/pamix.conf";

	if (Configuration::loadFile(&configuration, path))
		return;

	char *xdg_config_dirs = getenv("XDG_CONFIG_DIRS");

	path = xdg_config_dirs ? xdg_config_dirs : "/etc/xdg";
	path += "/pamix.conf";
	size_t cpos = path.find(':');
	while (cpos != std::string::npos) {
		if (Configuration::loadFile(&configuration, path.substr(0, cpos)))
			return;
		path = path.substr(cpos + 1, path.length() - cpos - 1);
		cpos = path.find(':');
	}
	if (Configuration::loadFile(&configuration, path))
		return;

	// if config file cant be loaded, use default bindings
	configuration.bind("q", "quit");
	configuration.bind("KEY_F(1)", "select-tab", "2");
	configuration.bind("KEY_F(2)", "select-tab", "3");
	configuration.bind("KEY_F(3)", "select-tab", "0");
	configuration.bind("KEY_F(4)", "select-tab", "1");

  configuration.bind("^I", "cycle-tab-next");

	configuration.bind("j", "select-next", "channel");
	configuration.bind("KEY_DOWN", "select-next", "channel");
	configuration.bind("J", "select-next");
	configuration.bind("k", "select-prev", "channel");
	configuration.bind("KEY_UP", "select-prev", "channel");
	configuration.bind("K", "select-prev");

	configuration.bind("h", "add-volume", "-0.05");
	configuration.bind("KEY_LEFT", "add-volume", "-0.05");
	configuration.bind("l", "add-volume", "0.05");
	configuration.bind("KEY_RIGHT", "add-volume", "0.05");

	configuration.bind("s", "cycle-next");
	configuration.bind("S", "cycle-prev");
	configuration.bind("c", "toggle-lock");
	configuration.bind("m", "toggle-mute");

	configuration.bind("1", "set-volume", "0.1");
	configuration.bind("2", "set-volume", "0.2");
	configuration.bind("3", "set-volume", "0.3");
	configuration.bind("4", "set-volume", "0.4");
	configuration.bind("5", "set-volume", "0.5");
	configuration.bind("6", "set-volume", "0.6");
	configuration.bind("7", "set-volume", "0.7");
	configuration.bind("8", "set-volume", "0.8");
	configuration.bind("9", "set-volume", "0.9");
	configuration.bind("0", "set-volume", "1.0");
}

void interfaceReconnectThread(PAInterface *interface) {
	while (running) {
		if (!interface->isConnected()) {
			interface->connect();
			signal_update(true);
		}
		sleep(5);
	}
}

void handleArguments(int argc, char **argv) {
	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if (arg == "--version") {
			printf("PAmix v%s\n", GIT_VERSION);
			exit(0);
		}
	}
}

int main(int argc, char **argv) {
	handleArguments(argc, argv);
	loadConfiguration();

	setlocale(LC_ALL, "");
	initscr();
	init_colors();
	nodelay(stdscr, true);
	curs_set(0);
	keypad(stdscr, true);
	meta(stdscr, true);
	noecho();

	signal(SIGABRT, sig_handle);
	signal(SIGSEGV, sig_handle);
	signal(SIGWINCH, sig_handle_resize);

	PAInterface pai("pamix");
	pamix_ui pamixUi(&pai);
	if (configuration.has(CONFIGURATION_AUTOSPAWN_PULSE))
		pai.setAutospawn(configuration.getBool(CONFIGURATION_AUTOSPAWN_PULSE));

	entry_type initialEntryType = ENTRY_SINKINPUT;
	if (configuration.has(CONFIGURATION_DEFAULT_TAB)) {
		int value = configuration.getInt(CONFIGURATION_DEFAULT_TAB);
		if (value >= 0 && value < ENTRY_COUNT)
			initialEntryType = (entry_type) value;
	}

	pamixUi.selectEntries(initialEntryType);

	pai.subscribe(&pai_subscription);
	pai.connect();

	pamix_setup(&pamixUi);
	pamixUi.redrawAll();

	std::thread(&interfaceReconnectThread, &pai).detach();

	std::thread inputT(inputThread, &pamixUi);
	inputT.detach();

	while (running) {
		std::unique_lock<std::mutex> lk(updMutex);
		cv.wait(lk, [] { return !updateDataQ.empty(); });

		if (updateDataQ.front().redrawAll)
			pamixUi.redrawAll();
		else
			pamixUi.redrawVolumeBars();
		updateDataQ.pop();
	}
	endwin();
	return 0;
}
