#include <pamix.hpp>

#include <unistd.h>
#include <configuration.hpp>
#include <condition_variable>
#include <queue>
#include <csignal>

// GLOBAL VARIABLES
Configuration configuration;

bool running = true;
unsigned selectedEntry = 0;
uint8_t selectedChannel = 0;
std::mutex screenMutex;

std::map<uint32_t, std::unique_ptr<Entry>> *entryMap = nullptr;
entry_type entryType;

// scrolling
uint32_t numSkippedEntries = 0;
uint32_t numDisplayedEntries = 0;

std::map<uint32_t, uint32_t> monitorVolumeBarLineNum;
std::map<uint32_t, uint8_t> entrySizes;

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

void selectEntries(PAInterface *interface, entry_type type) {
	switch (type) {
		case ENTRY_SINK:
			entryMap = &interface->getSinks();
			break;
		case ENTRY_SOURCE:
			entryMap = &interface->getSources();
			break;
		case ENTRY_SINKINPUT:
			entryMap = &interface->getSinkInputs();
			break;
		case ENTRY_SOURCEOUTPUT:
			entryMap = &interface->getSourceOutputs();
			break;
		case ENTRY_CARDS:
			entryMap = &interface->getCards();
			break;
		default:
			return;
	}
	entryType = type;
}

void generateMeter(int y, int x, int width, double pct, const double maxvol) {

	int segments = width - 2;

	if (segments <= 0)
		return;

	if (pct < 0)
		pct = 0;
	else if (pct > maxvol)
		pct = maxvol;

	auto filled = (unsigned) (pct / maxvol * (double) segments);
	if (filled > segments)
		filled = (unsigned) segments;

	mvaddstr(y, x++, "[");
	mvaddstr(y, x + segments, "]");

	auto indexColorA = (int) (segments * ((double) 1 / 3));
	auto indexColorB = (int) (segments * ((double) 2 / 3));

	FEAT_UNICODE_STRING meter;

	meter.append(filled, SYM_VOLBAR);
	meter.append((unsigned) segments - filled, SYM_SPACE);
	attron(COLOR_PAIR(1));
	FEAT_UNICODE_MVADDNSTR(y, x, meter.c_str(), indexColorA);
	attroff(COLOR_PAIR(1));
	attron(COLOR_PAIR(2));
	FEAT_UNICODE_MVADDNSTR(y, x + indexColorA, meter.c_str() + indexColorA, indexColorB - indexColorA);
	attroff(COLOR_PAIR(2));
	attron(COLOR_PAIR(3));
	FEAT_UNICODE_MVADDNSTR(y, x + indexColorB, meter.c_str() + indexColorB, segments - indexColorB);
	attroff(COLOR_PAIR(3));
}

void drawEntries(PAInterface *interface) {
	std::lock_guard<std::mutex> modifyLockGuard(interface->m_ModifyMutex);
	std::lock_guard<std::mutex> screenLockGuard(screenMutex);

	if (selectedEntry > entryMap->size() - 1) {
		selectedEntry = (unsigned) (entryMap->size() - 1);
		selectedChannel = 0;
	}

	clear();

	mvprintw(0, 1, "%d/%d", entryMap->empty() ? 0 : selectedEntry + 1, entryMap->size());
	mvprintw(0, 10, "%s", entryTypeNames[entryType]);

	unsigned y = 2;
	unsigned index = 0;

	auto entryIter = entryMap->begin();
	for (; entryIter != entryMap->end(); entryIter++, index++)
		entrySizes[index] = entryIter->second->m_Lock ? (char) 1 : entryIter->second->m_PAVolume.channels;

	for (entryIter = std::next(entryMap->begin(), numSkippedEntries), index = numSkippedEntries;
	     entryIter != entryMap->end(); entryIter++, index++) {
		std::string appname = entryIter->second ? entryIter->second->m_Name : "";
		pa_volume_t avgvol = pa_cvolume_avg(&entryIter->second->m_PAVolume);
		double dB = pa_sw_volume_to_dB(avgvol);
		double vol = avgvol / (double) PA_VOLUME_NORM;

		bool isSelectedEntry = index == selectedEntry;
		uint8_t numChannels = entryIter->second->m_Lock ? (char) 1 : entryIter->second->m_PAVolume.channels;

		// break if not enough space
		if (y + numChannels + 2 > (unsigned) LINES)
			break;

		if (entryIter->second->m_Meter && entryIter->second->m_Lock) {
			generateMeter(y, 32, COLS - 33, vol, MAX_VOL);

			std::string descstring = "%.2fdB (%.2f)";
			if (isSelectedEntry)
				descstring.insert(0, SYM_ARROW);
			mvprintw(y++, 1, descstring.c_str(), dB, vol);
		} else if (entryIter->second->m_Meter) {
			for (uint32_t chan = 0; chan < numChannels; chan++) {
				std::string channelDescriptionTemplate = "%.*s  %.2fdB (%.2f)";

				bool isSelChannel = isSelectedEntry && chan == selectedChannel;
				double volumeDecibel = pa_sw_volume_to_dB(entryIter->second->m_PAVolume.values[chan]);
				double volumePercent = entryIter->second->m_PAVolume.values[chan] / (double) PA_VOLUME_NORM;
				pa_channel_position_t channelPosition = entryIter->second->m_PAChannelMap.map[chan];
				std::string channelPrettyName = pa_channel_position_to_pretty_string(channelPosition);

				generateMeter(y, 32, COLS - 33, volumePercent, MAX_VOL);

				if (isSelChannel)
					channelDescriptionTemplate.insert(0, SYM_ARROW);

				mvprintw(y++, 1, channelDescriptionTemplate.c_str(),
				         isSelChannel ? 13 : 15, channelPrettyName.c_str(),
				         volumeDecibel, volumePercent);
			}
		}

		double peak = entryIter->second->m_Peak;

		monitorVolumeBarLineNum[entryIter->first] = y;
		if (entryIter->second->m_Meter)
			generateMeter(y++, 1, COLS - 2, peak, 1.0);

		if (isSelectedEntry)
			attron(A_STANDOUT);

		if (appname.length() > COLS * 0.4) {
			appname = appname.substr(0, (unsigned long) (COLS * 0.4 - 2));
			appname.append("..");
		}
		mvprintw(y++, 1, appname.c_str());
		if (isSelectedEntry)
			attroff(A_STANDOUT);
		bool muted = entryType != ENTRY_CARDS && (entryIter->second->m_Mute || avgvol == PA_VOLUME_MUTED);
		printw(" %s %s", muted ? SYM_MUTE : "", entryIter->second->m_Lock ? SYM_LOCK : "");

		//append entryDisplayName
		int px = 0, py = 0;
		unsigned space = 0;
		getyx(stdscr, py, px);
		space = (unsigned) COLS - px - 3;

		std::string entryDisplayName;
		switch (entryType) {
			case ENTRY_SINK:
			case ENTRY_SOURCE: {
				auto deviceEntry = ((DeviceEntry *) entryIter->second.get());
				if (deviceEntry != nullptr) {
					const DeviceEntry::DeviceProfile *deviceProfile = deviceEntry->getPortProfile();
					if (deviceProfile != nullptr)
						entryDisplayName = deviceProfile->description.empty() ? deviceProfile->name : deviceProfile->description;
				}
				break;
			}
			case ENTRY_SINKINPUT:
				if (entryIter->second && interface->getSinks()[((SinkInputEntry *) entryIter->second.get())->m_Device])
					entryDisplayName = interface->getSinks()[((SinkInputEntry *) entryIter->second.get())->m_Device]->m_Name;
				break;
			case ENTRY_SOURCEOUTPUT:
				if (entryIter->second && interface->getSources()[((SourceOutputEntry *) entryIter->second.get())->m_Device])
					entryDisplayName = interface->getSources()[((SourceOutputEntry *) entryIter->second.get())->m_Device]->m_Name;
				break;
			case ENTRY_CARDS:
				if (entryIter->second)
					entryDisplayName = ((CardEntry *) entryIter->second.get())->m_Profiles[((CardEntry *) entryIter->second.get())->m_Profile].description;
				break;
			default:
				break;
		}
		if (space < entryDisplayName.size()) {
			entryDisplayName = entryDisplayName.substr(0, space - 2);
			entryDisplayName.append("..");
			space = 0;
		} else {
			space -= entryDisplayName.size();
		}
		mvprintw(py, px + space + 1, entryDisplayName.c_str());

		y += 1;
	}

	numDisplayedEntries = index - numSkippedEntries;

	refresh();
}

void drawMonitors(PAInterface *interface) {
	std::lock_guard<std::mutex> lg(screenMutex);
	std::lock_guard<std::mutex> modlg(interface->m_ModifyMutex);
	auto it = std::next(entryMap->begin(), numSkippedEntries);
	uint32_t index = 0;
	for (; it != entryMap->end(); it++, index++) {
		if (index >= numSkippedEntries + numDisplayedEntries)
			break;
		uint32_t y = monitorVolumeBarLineNum[it->first];
		if (it->second->m_Meter)
			generateMeter(y, 1, COLS - 2, it->second->m_Peak, 1.0);
	}
	refresh();
}

inline iter_entry_t get_selected_entry_iter(PAInterface *) {
	if (selectedEntry < entryMap->size())
		return std::next(entryMap->begin(), selectedEntry);
	else
		return entryMap->end();
}

void set_volume(PAInterface *interface, double pct) {
	auto it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->setVolume(it->second->m_Lock ? -1 : selectedChannel, (pa_volume_t) (PA_VOLUME_NORM * pct));
}

void add_volume(PAInterface *interface, double pct) {
	auto it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->addVolume(it->second->m_Lock ? -1 : selectedChannel, pct);
}

void cycle_switch(PAInterface *interface, bool increment) {
	auto it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->cycleSwitch(increment);
}

void set_mute(PAInterface *interface, bool mute) {
	auto it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->setMute(mute);
}

void toggle_mute(PAInterface *interface) {
	auto it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->setMute(!it->second->m_Mute);
}

void adjustDisplay() {
	if (!entryMap->empty())
		return;
	if (selectedEntry >= numSkippedEntries && selectedEntry < numSkippedEntries + numDisplayedEntries)
		return;
	if (selectedEntry < numSkippedEntries) {
		// scroll up until selected is at top
		numSkippedEntries = selectedEntry;
	} else {
		// scroll down until selected is at bottom
		uint32_t linesToFree = 0;
		uint32_t idx = numSkippedEntries + numDisplayedEntries;
		for (; idx <= selectedEntry; idx++)
			linesToFree += entrySizes[idx] + 2;

		uint32_t linesFreed = 0;
		idx = numSkippedEntries;
		while (linesFreed < linesToFree)
			linesFreed += entrySizes[idx++] + 2;
		numSkippedEntries = idx;
	}
}

void select_next(PAInterface *interface, bool channelLevel) {
	if (selectedEntry < entryMap->size()) {
		if (channelLevel) {
			auto it = get_selected_entry_iter(interface);
			if (!it->second->m_Lock && selectedChannel < it->second->m_PAVolume.channels - 1) {
				selectedChannel++;
				return;
			}
		}
		selectedEntry++;
		if (selectedEntry >= entryMap->size())
			selectedEntry = (unsigned int) entryMap->size() - 1;
		else
			selectedChannel = 0;
	}
	adjustDisplay();
}

void select_previous(PAInterface *interface, bool channelLevel) {
	if (selectedEntry < entryMap->size()) {
		if (channelLevel) {
			auto it = get_selected_entry_iter(interface);
			if (!it->second->m_Lock && selectedChannel > 0) {
				selectedChannel--;
				return;
			}
		}
		if (selectedEntry > 0)
			selectedEntry--;
		else if (!get_selected_entry_iter(interface)->second->m_Lock) {
			selectedChannel = get_selected_entry_iter(interface)->second->m_PAVolume.channels - (char) 1;
		}
	}
	adjustDisplay();
}

void set_lock(PAInterface *interface, bool lock) {
	auto it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->m_Lock = lock;
}

void toggle_lock(PAInterface *interface) {
	auto it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->m_Lock = !it->second->m_Lock;
}

void inputThread(PAInterface *) {
	while (running) {
		screenMutex.lock();
		int ch = getch();
		screenMutex.unlock();
		if (ch != ERR && ch != KEY_RESIZE && ch != KEY_MOUSE) {
			configuration.pressKey(ch);
		}
		usleep(2000);
	}
}

void pai_subscription(PAInterface *, pai_subscription_type_t type) {
	bool updAll = (type & PAI_SUBSCRIPTION_MASK_INFO) != 0;
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

	path = xdg_config_dirs ? xdg_config_dirs : "/etc";
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
	if (configuration.has(CONFIGURATION_AUTOSPAWN_PULSE))
		pai.setAutospawn(configuration.getBool(CONFIGURATION_AUTOSPAWN_PULSE));

	entry_type entry = ENTRY_SINKINPUT;
	if (configuration.has(CONFIGURATION_DEFAULT_TAB)) {
		int value = configuration.getInt(CONFIGURATION_DEFAULT_TAB);
		if (value >= 0 && value < ENTRY_COUNT)
			entry = (entry_type) value;
	}
	selectEntries(&pai, entry);

	pai.subscribe(&pai_subscription);
	if (!pai.connect()) {
		endwin();
		fprintf(stderr, "Failed to connect to PulseAudio.\n");
		exit(1);
	}

	pamix_set_interface(&pai);

	drawEntries(&pai);
	std::thread inputT(inputThread, &pai);
	inputT.detach();

	while (running) {
		std::unique_lock<std::mutex> lk(updMutex);
		cv.wait(lk, [] { return !updateDataQ.empty(); });

		if (updateDataQ.front().redrawAll)
			drawEntries(&pai);
		else
			drawMonitors(&pai);
		updateDataQ.pop();
	}
	endwin();
	return 0;
}
