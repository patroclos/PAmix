#include <pamix.hpp>

#include <condition_variable>
#include <configuration.hpp>
#include <locale.h>
#include <mutex>
#include <ncurses.h>
#include <queue>
#include <signal.h>
#include <string>
#include <thread>

// GLOBAL VARIABLES
Configuration configuration;

bool     running         = true;
unsigned selectedEntry   = 0;
uint8_t  selectedChannel = 0;
std::map<uint32_t, double> lastPeaks;
std::mutex screenMutex;

std::map<uint32_t, std::unique_ptr<Entry>> *entryMap = nullptr;
entry_type entryType;

// scrolling
uint32_t skipEntries         = 0;
uint32_t numDisplayedEntries = 0;

std::map<uint32_t, uint32_t> mapMonitorLines;
std::map<uint32_t, uint8_t>  mapIndexSize;

// sync main and callback threads
std::mutex              updMutex;
std::condition_variable cv;

std::queue<UpdateData> updateDataQ;

void quit()
{
	running = false;
	signal_update(false);
}

void signal_update(bool all)
{
	{
		std::lock_guard<std::mutex> lk(updMutex);
		updateDataQ.push(UpdateData(all));
	}
	cv.notify_one();
}

void selectEntries(PAInterface *interface, entry_type type)
{
	switch (type)
	{
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
	default:
		return;
	}
	entryType = type;
}

void generateMeter(int y, int x, int width, double pct, const double maxvol)
{
	int segments = width - 2;
	if (segments <= 0)
		return;
	if (pct < 0)
		pct = 0;
	else if (pct > maxvol)
		pct    = maxvol;
	int filled = pct / maxvol * (double)segments;
	if (filled > segments)
		filled = segments;

	mvaddstr(y, x++, "[");
	mvaddstr(y, x + segments, "]");

	int indexColorA = segments * ((double)1 / 3);
	int indexColorB = segments * ((double)2 / 3);

	FEAT_UNICODE_STRING meter;

	meter.append(filled, SYM_VOLBAR);
	meter.append(segments - filled, SYM_SPACE);
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

void drawEntries(PAInterface *interface)
{
	std::lock_guard<std::mutex> modlg(interface->m_ModifyMutex);
	std::lock_guard<std::mutex> lg(screenMutex);

	if (selectedEntry > entryMap->size() - 1)
	{
		selectedEntry   = entryMap->size() - 1;
		selectedChannel = 0;
	}

	clear();

	mvprintw(0, 1, "%d/%d", entryMap->empty() ? 0 : selectedEntry + 1, entryMap->size());
	const char *entryname = entryType == ENTRY_SINK ? "Output Devices" : entryType == ENTRY_SOURCE ? "Input Devices" : entryType == ENTRY_SINKINPUT ? "Playback" : "Recording";
	mvprintw(0, 10, "%s", entryname);

	unsigned y     = 2;
	unsigned index = 0;

	iter_entry_t it = entryMap->begin();
	for (; it != entryMap->end(); it++, index++)
		mapIndexSize[index] = it->second->m_Lock ? 1 : it->second->m_PAVolume.channels;

	for (it = std::next(entryMap->begin(), skipEntries), index = skipEntries; it != entryMap->end(); it++, index++)
	{
		std::string appname = it->second ? it->second->m_Name : "";
		pa_volume_t avgvol  = pa_cvolume_avg(&it->second->m_PAVolume);
		double      dB      = pa_sw_volume_to_dB(avgvol);
		double      vol     = avgvol / (double)PA_VOLUME_NORM;

		bool    isSelectedEntry = index == selectedEntry;
		uint8_t numChannels     = it->second->m_Lock ? 1 : it->second->m_PAVolume.channels;

		if (y + numChannels + 2 > (unsigned)LINES)
			break;

		if (it->second->m_Meter && it->second->m_Lock)
		{
			generateMeter(y, 32, COLS - 33, vol, MAX_VOL);

			std::string descstring = "%.2fdB (%.2f)";
			if (isSelectedEntry)
				descstring.insert(0, SYM_ARROW);
			mvprintw(y++, 1, descstring.c_str(), dB, vol);
		}
		else if (it->second->m_Meter)
		{
			for (uint32_t chan = 0; chan < numChannels; chan++)
			{
				bool        isSelChannel = isSelectedEntry && chan == selectedChannel;
				std::string channame     = pa_channel_position_to_pretty_string(it->second->m_PAChannelMap.map[chan]);
				double      cdB          = pa_sw_volume_to_dB(it->second->m_PAVolume.values[chan]);
				double      cvol         = it->second->m_PAVolume.values[chan] / (double)PA_VOLUME_NORM;
				generateMeter(y, 32, COLS - 33, cvol, MAX_VOL);
				std::string descstring = "%.*s  %.2fdB (%.2f)";
				if (isSelChannel)
					descstring.insert(0, SYM_ARROW);

				mvprintw(y++, 1, descstring.c_str(), isSelChannel ? 13 : 15, channame.c_str(), cdB, cvol);
			}
		}

		double peak = it->second->m_Peak;

		mapMonitorLines[it->first] = y;
		if (it->second->m_Meter)
			generateMeter(y++, 1, COLS - 2, peak, 1.0);

		if (isSelectedEntry)
			attron(A_STANDOUT);

		if (appname.length() > COLS * 0.4)
		{
			appname = appname.substr(0, COLS * 0.4 - 2);
			appname.append("..");
		}
		mvprintw(y++, 1, appname.c_str());
		if (isSelectedEntry)
			attroff(A_STANDOUT);
		bool muted = it->second->m_Mute || avgvol == PA_VOLUME_MUTED;
		printw(" %s %s", muted ? SYM_MUTE : "", it->second->m_Lock ? SYM_LOCK : "");

		//append sinkname
		int      px = 0, py = 0;
		unsigned space = 0;
		getyx(stdscr, py, px);
		space = COLS - px - 3;

		std::string sinkname = "";
		switch (entryType)
		{
		case ENTRY_SINK:
			if (it->second)
				sinkname = ((SinkEntry *)it->second.get())->getPort();
			break;
		case ENTRY_SOURCE:
			if (it->second)
				sinkname = ((SourceEntry *)it->second.get())->getPort();
			break;
		case ENTRY_SINKINPUT:
			if (it->second && interface->getSinks()[((SinkInputEntry *)it->second.get())->m_Device])
				sinkname = interface->getSinks()[((SinkInputEntry *)it->second.get())->m_Device]->m_Name;
			break;
		case ENTRY_SOURCEOUTPUT:
			if (it->second && interface->getSources()[((SourceOutputEntry *)it->second.get())->m_Device])
				sinkname = interface->getSources()[((SourceOutputEntry *)it->second.get())->m_Device]->m_Name;
			break;
		default:
			break;
		}
		if (space < sinkname.size())
		{
			sinkname = sinkname.substr(0, space - 2);
			sinkname.append("..");
			space = 0;
		}
		else
		{
			space -= sinkname.size();
		}
		mvprintw(py, px + space + 1, sinkname.c_str());

		y += 1;
	}

	numDisplayedEntries = index - skipEntries;

	refresh();
}

void drawMonitors(PAInterface *interface)
{
	std::lock_guard<std::mutex> lg(screenMutex);
	std::lock_guard<std::mutex> modlg(interface->m_ModifyMutex);
	iter_entry_t                it    = std::next(entryMap->begin(), skipEntries);
	uint32_t                    index = 0;
	for (; it != entryMap->end(); it++, index++)
	{
		if (index >= skipEntries + numDisplayedEntries)
			break;
		uint32_t y = mapMonitorLines[it->first];
		if (it->second->m_Meter)
			generateMeter(y, 1, COLS - 2, it->second->m_Peak, 1.0);
	}
	refresh();
}

inline iter_entry_t get_selected_entry_iter(PAInterface *interface)
{
	if (selectedEntry < entryMap->size())
		return std::next(entryMap->begin(), selectedEntry);
	else
		return entryMap->end();
}

void set_volume(PAInterface *interface, double pct)
{
	iter_entry_t it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->setVolume(it->second->m_Lock ? -1 : selectedChannel, PA_VOLUME_NORM * pct);
}

void add_volume(PAInterface *interface, double pct)
{
	iter_entry_t it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->addVolume(it->second->m_Lock ? -1 : selectedChannel, pct);
}

void cycle_switch(PAInterface *interface, bool increment)
{
	iter_entry_t it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->cycleSwitch(increment);
}

void set_mute(PAInterface *interface, bool mute)
{
	iter_entry_t it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->setMute(mute);
}
void toggle_mute(PAInterface *interface)
{
	iter_entry_t it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->setMute(!it->second->m_Mute);
}

void adjustDisplay()
{
	if (!entryMap->size())
		return;
	if (selectedEntry >= skipEntries && selectedEntry < skipEntries + numDisplayedEntries)
		return;
	if (selectedEntry < skipEntries)
	{
		// scroll up until selected is at top
		skipEntries = selectedEntry;
	}
	else
	{
		// scroll down until selected is at bottom
		uint32_t linesToFree = 0;
		uint32_t idx         = skipEntries + numDisplayedEntries;
		for (; idx <= selectedEntry; idx++)
			linesToFree += mapIndexSize[idx] + 2;

		uint32_t linesFreed = 0;
		idx                 = skipEntries;
		while (linesFreed < linesToFree)
			linesFreed += mapIndexSize[idx++] + 2;
		skipEntries = idx;
	}
}

void select_next(PAInterface *interface, bool channelLevel)
{
	if (selectedEntry < entryMap->size())
	{
		if (channelLevel)
		{
			iter_entry_t it = get_selected_entry_iter(interface);
			if (!it->second->m_Lock && selectedChannel < it->second->m_PAVolume.channels - 1)
			{
				selectedChannel++;
				return;
			}
		}
		selectedEntry++;
		if (selectedEntry >= entryMap->size())
			selectedEntry = entryMap->size() - 1;
		else
			selectedChannel = 0;
	}
	adjustDisplay();
}

void select_previous(PAInterface *interface, bool channelLevel)
{
	if (selectedEntry < entryMap->size())
	{
		if (channelLevel)
		{
			iter_entry_t it = get_selected_entry_iter(interface);
			if (!it->second->m_Lock && selectedChannel > 0)
			{
				selectedChannel--;
				return;
			}
		}
		if (selectedEntry > 0)
			selectedEntry--;
		else if (!get_selected_entry_iter(interface)->second->m_Lock)
		{
			selectedChannel = get_selected_entry_iter(interface)->second->m_PAVolume.channels - 1;
		}
	}
	adjustDisplay();
}

void set_lock(PAInterface *interface, bool lock)
{
	iter_entry_t it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->m_Lock = lock;
}
void toggle_lock(PAInterface *interface)
{
	iter_entry_t it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->m_Lock = !it->second->m_Lock;
}

void inputThread(PAInterface *interface)
{
	while (running)
	{
		int ch = getch();
		configuration.pressKey(ch);
		continue;
	}
}

void pai_subscription(PAInterface *interface, pai_subscription_type_t type)
{
	bool updAll = type & PAI_SUBSCRIPTION_MASK_INFO;
	signal_update(updAll);
}

void sig_handle_resize(int s)
{
	endwin();
	refresh();
	signal_update(true);
}

void init_colors()
{
	start_color();
#ifdef HAVE_USE_DEFAULT_COLORS
	use_default_colors();
	init_pair(1, COLOR_GREEN, -1);
	init_pair(2, COLOR_YELLOW, -1);
	init_pair(3, COLOR_RED, -1);
#else
	init_pair(1, COLOR_GREEN, 0);
	init_pair(2, COLOR_YELLOW, 0);
	init_pair(3, COLOR_RED, 0);
#endif
}

void sig_handle(int sig)
{
	endwin();
}

void loadConfiguration()
{
	char *home            = getenv("HOME");
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
	while (cpos != std::string::npos)
	{
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

int main(int argc, char **argv)
{
	loadConfiguration();

	setlocale(LC_ALL, "");
	initscr();
	init_colors();
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
	if (configuration.has(CONFIGURATION_DEFAULT_TAB))
	{
		int value = configuration.getInt(CONFIGURATION_DEFAULT_TAB);
		if (value >= 0 && value < ENTRY_COUNT)
			entry = (entry_type) value;
	}
	selectEntries(&pai, entry);

	pai.subscribe(pai_subscription);
	if (!pai.connect())
	{
		endwin();
		fprintf(stderr, "Failed to connect to PulseAudio.\n");
		exit(1);
	}

	pamix_set_interface(&pai);

	drawEntries(&pai);
	std::thread inputT(inputThread, &pai);
	inputT.detach();

	while (running)
	{
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
