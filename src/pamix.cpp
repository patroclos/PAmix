#include <condition_variable>
#include <locale.h>
#include <mutex>
#include <ncurses.h>
#include <painterface.hpp>
#include <queue>
#include <signal.h>
#include <string>
#include <thread>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

struct UpdateData
{
	bool redrawAll;
	UpdateData() = default;
	UpdateData(bool redrawAll) { this->redrawAll = redrawAll; }
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

// GLOBAL VARIABLES
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

void generateMeter(int y, int x, int width, const double pct, const double maxvol)
{
	int segments = width - 2;
	if (segments <= 0)
		return;
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
	interface->modifyLock();
	std::lock_guard<std::mutex> lg(screenMutex);

	if (selectedEntry > entryMap->size() - 1)
	{
		selectedEntry   = entryMap->size() - 1;
		selectedChannel = 0;
	}

	clear();

	mvprintw(0, 1, "%d/%d", selectedEntry + 1, entryMap->size());
	const char *entryname = entryType == ENTRY_SINK ? "Output Devices" : entryType == ENTRY_SOURCE ? "Input Devices" : entryType == ENTRY_SINKINPUT ? "Playback" : "Recording";
	mvprintw(0, 10, "%s", entryname);

	unsigned y     = 2;
	unsigned index = 0;

	iter_entry_t it = entryMap->begin();
	for (; it != entryMap->end(); it++, index++)
		mapIndexSize[index] = it->second->m_Lock ? 1 : it->second->m_PAVolume.channels;

	for (it = std::next(entryMap->begin(), skipEntries), index = skipEntries; it != entryMap->end(); it++, index++)
	{
		std::string &appname = it->second->m_Name;
		pa_volume_t  avgvol  = pa_cvolume_avg(&it->second->m_PAVolume);
		double       dB      = pa_sw_volume_to_dB(avgvol);
		double       vol     = avgvol / (double)PA_VOLUME_NORM;

		bool    isSelectedEntry = index == selectedEntry;
		uint8_t numChannels     = it->second->m_Lock ? 1 : it->second->m_PAVolume.channels;

		if (y + numChannels + 2 > (unsigned)LINES)
			break;

		if (it->second->m_Lock)
		{
			generateMeter(y, 32, COLS - 33, vol, MAX_VOL);

			std::string descstring = "%.2fdB (%.2f)";
			if (isSelectedEntry)
				descstring.insert(0, SYM_ARROW);
			mvprintw(y++, 1, descstring.c_str(), dB, vol);
		}
		else
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
		generateMeter(y++, 1, COLS - 2, peak, 1.0);

		if (isSelectedEntry)
			attron(A_STANDOUT);

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
			sinkname = ((SinkEntry *)it->second.get())->getPort();
			break;
		case ENTRY_SOURCE:
			sinkname = ((SourceEntry *)it->second.get())->getPort();
			break;
		case ENTRY_SINKINPUT:
			sinkname = interface->getSinks()[((SinkInputEntry *)it->second.get())->m_Device]->m_Name;
			break;
		case ENTRY_SOURCEOUTPUT:
			sinkname = interface->getSources()[((SourceOutputEntry *)it->second.get())->m_Device]->m_Name;
			break;
		default:
			break;
		}
		if (space < sinkname.size())
		{
			sinkname = sinkname.substr(0, space - 3);
			sinkname.append("...");
			space = 0;
		}
		else
		{
			space -= sinkname.size();
		}
		mvprintw(py, px + space, sinkname.c_str());

		y += 1;
	}

	numDisplayedEntries = index - skipEntries;

	interface->modifyUnlock();
	refresh();
}

void drawMonitors(PAInterface *interface)
{
	std::lock_guard<std::mutex> lg(screenMutex);
	interface->modifyLock();
	iter_entry_t it    = std::next(entryMap->begin(), skipEntries);
	uint32_t     index = 0;
	for (; it != entryMap->end(); it++, index++)
	{
		if (index >= skipEntries + numDisplayedEntries)
			break;
		uint32_t y = mapMonitorLines[it->first];
		generateMeter(y, 1, COLS - 2, it->second->m_Peak, 1.0);
	}
	interface->modifyUnlock();
	refresh();
}

inline iter_entry_t get_selected_entry_iter(PAInterface *interface)
{
	if (selectedEntry < entryMap->size())
		return std::next(entryMap->begin(), selectedEntry);
	else
		return entryMap->end();
}

inline void set_volume(double pct, PAInterface *interface)
{
	iter_entry_t it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->setVolume(interface, it->second->m_Lock ? -1 : selectedChannel, PA_VOLUME_NORM * pct);
}

inline void add_volume(double pctDelta, PAInterface *interface)
{
	iter_entry_t it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->addVolume(interface, it->second->m_Lock ? -1 : selectedChannel, pctDelta);
}

inline void cycleSwitch(bool increment, PAInterface *interface)
{
	iter_entry_t it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->cycleSwitch(interface, increment);
}

inline void mute_entry(PAInterface *interface)
{
	iter_entry_t it = get_selected_entry_iter(interface);
	if (it != entryMap->end())
		it->second->setMute(interface, !it->second->m_Mute);
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

void selectNext(PAInterface *interface, bool channelLevel = true)
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

void selectPrev(PAInterface *interface, bool channelLevel = true)
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

void toggleChannelLock(PAInterface *interface)
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
		switch (ch)
		{
		case KEY_F(1):
			selectEntries(interface, ENTRY_SINKINPUT);
			signal_update(true);
			break;
		case KEY_F(2):
			selectEntries(interface, ENTRY_SOURCEOUTPUT);
			signal_update(true);
			break;
		case KEY_F(3):
			selectEntries(interface, ENTRY_SINK);
			signal_update(true);
			break;
		case KEY_F(4):
			selectEntries(interface, ENTRY_SOURCE);
			signal_update(true);
			break;
		case '1':
			set_volume(0.1,interface);
			break;
		case '2':
			set_volume(0.2,interface);
			break;
		case '3':
			set_volume(0.3,interface);
			break;
		case '4':
			set_volume(0.4,interface);
			break;
		case '5':
			set_volume(0.5,interface);
			break;
		case '6':
			set_volume(0.6,interface);
			break;
		case '7':
			set_volume(0.7,interface);
			break;
		case '8':
			set_volume(0.8,interface);
			break;
		case '9':
			set_volume(0.9,interface);
			break;
		case '0':
			set_volume(1.0,interface);
			break;
		case 'h':
			add_volume(-0.05, interface);
			break;
		case 'H':
			add_volume(-0.15, interface);
			break;
		case 'l':
			add_volume(0.05, interface);
			break;
		case 'L':
			add_volume(0.15, interface);
			break;
		case 'j':
			selectNext(interface);
			signal_update(true);
			break;
		case 'J':
			selectNext(interface, false);
			signal_update(true);
			break;
		case 'k':
			selectPrev(interface);
			signal_update(true);
			break;
		case 'K':
			selectPrev(interface, false);
			signal_update(true);
			break;
		case 's':
			cycleSwitch(true, interface);
			break;
		case 'S':
			cycleSwitch(false, interface);
			break;
		case 'm':
			mute_entry(interface);
			break;
		case 'c':
			toggleChannelLock(interface);
			signal_update(true);
			break;
		case 'q':
			running = false;
			signal_update(false);
			break;
		default:
			break;
		}
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
	init_pair(1, COLOR_GREEN, 0);
	init_pair(2, COLOR_YELLOW, 0);
	init_pair(3, COLOR_RED, 0);
}

void sig_handle_abort(int sig)
{
	endwin();
}

int main(int argc, char **argv)
{
	setlocale(LC_ALL, "");
	initscr();
	init_colors();
	curs_set(0);
	keypad(stdscr, true);
	noecho();

	signal(SIGABRT, sig_handle_abort);
	signal(SIGWINCH, sig_handle_resize);

	PAInterface pai("pamix");
	selectEntries(&pai, ENTRY_SINKINPUT);
	pai.subscribe(pai_subscription);
	if (!pai.connect())
	{
		endwin();
		fprintf(stderr, "Failed to connect to PulseAudio.\n");
		exit(1);
	}

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
