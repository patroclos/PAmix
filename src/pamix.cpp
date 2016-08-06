#include <condition_variable>
#include <locale.h>
#include <mutex>
#include <ncursesw/ncurses.h>
#include <painterface.h>
#include <queue>
#include <signal.h>
#include <string>
#include <thread>

struct UpdateData
{
	bool redrawAll;
	UpdateData() = default;
	UpdateData(bool redrawAll) { this->redrawAll = redrawAll; }
};

#define DECAY_STEP 0.04
#define MAX_VOL 1.5
#define SYM_VOLBAR L"\u25ae" //▮
#define SYM_SPACE L"\u0020" // White space

// GLOBAL VARIABLES
bool     running         = true;
unsigned selectedInput   = 0;
uint8_t  selectedChannel = 0;
std::map<uint32_t, double> lastPeaks;
std::mutex screenMutex;

// scrolling
uint32_t skipInputs         = 0;
uint32_t numDisplayedInputs = 0;

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

void generateMeter(int y, int x, int width, const double pct, const double maxvol)
{
	int segments = width - 2;
	if (segments <= 0)
		return;
	int filled = pct / maxvol * (double)segments;
	if (filled > segments)
		filled = segments;

	mvaddwstr(y, x, L"[");
	x++;

	for (int i = 0; i < filled; i++)
	{
		float p = (float)i / width;
		int   colorPair;

		if (p < .333f)
			colorPair = COLOR_PAIR(1);
		else if (p > .333f && p < .666f)
			colorPair = COLOR_PAIR(2);
		else
			colorPair = COLOR_PAIR(3);

		attron(colorPair);
		mvaddwstr(y, x, SYM_VOLBAR);
		attroff(colorPair);
		x++;
	}

	// Fill up the rest with spaces
	for (int i = 0; i < (segments - filled); i++)
	{
		mvaddwstr(y, x, SYM_SPACE);
		x++;
	}
	mvaddwstr(y, x, L"]");
	x++;
}

void updatesinks(PAInterface *interface)
{
	interface->modifyLock();
	//avoid concurrent drawing
	std::lock_guard<std::mutex> lg(screenMutex);

	if (selectedInput > interface->getInputInfo().size() - 1)
	{
		selectedInput   = interface->getInputInfo().size() - 1;
		selectedChannel = 0;
	}

	clear();

	mvprintw(0, 1, "Selected %d/%d", selectedInput + 1, interface->getInputInfo().size());
	unsigned y     = 1;
	unsigned index = 0;

	iter_inputinfo_t it = interface->getInputInfo().begin();
	for (; it != interface->getInputInfo().end(); it++, index++)
		mapIndexSize[index] = it->second.m_ChannelsLocked ? 1 : it->second.m_PAVolume.channels + 2;

	for (it = std::next(interface->getInputInfo().begin(), skipInputs), index = skipInputs; it != interface->getInputInfo().end(); it++, index++)
	{
		std::string &appname = it->second.m_Appname;
		pa_volume_t  avgvol  = it->second.getAverageVolume();
		double       dB      = pa_sw_volume_to_dB(avgvol);
		double       vol     = avgvol / (double)PA_VOLUME_NORM;

		bool    isSelInput  = index == selectedInput;
		uint8_t numChannels = it->second.m_ChannelsLocked ? 1 : it->second.m_PAVolume.channels;

		if (y + numChannels + 2 > (unsigned)LINES)
			break;

		if (it->second.m_ChannelsLocked)
		{
			generateMeter(y, 32, COLS - 33, vol, MAX_VOL);

			std::string descstring = "%.2fdB (%.2f)";
			if (isSelInput)
				descstring.insert(0, "▶ ");
			mvprintw(y++, 1, descstring.c_str(), dB, vol);
		}
		else
		{
			for (uint32_t chan = 0; chan < numChannels; chan++)
			{
				bool        isSelChannel = isSelInput && chan == selectedChannel;
				std::string channame     = pa_channel_position_to_pretty_string(it->second.m_PAChannelMap.map[chan]);
				double      cdB          = pa_sw_volume_to_dB(it->second.m_PAVolume.values[chan]);
				double      cvol         = it->second.m_PAVolume.values[chan] / (double)PA_VOLUME_NORM;
				generateMeter(y, 32, COLS - 33, cvol, MAX_VOL);
				std::string descstring = "%.*s  %.2fdB (%.2f)";
				if (isSelChannel)
					descstring.insert(0, "▶ ");

				mvprintw(y++, 1, descstring.c_str(), isSelChannel ? 13 : 15, channame.c_str(), cdB, cvol);
			}
		}

		double peak = it->second.m_Peak;

		mapMonitorLines[it->first] = y;
		generateMeter(y++, 1, COLS - 2, peak, 1.0);

		//mark selected input with arrow in front of name
		if (isSelInput)
			attron(A_STANDOUT);

		mvprintw(y++, 1, appname.c_str());
		if (isSelInput)
			attroff(A_STANDOUT);
		bool muted = it->second.m_Mute || avgvol == PA_VOLUME_MUTED;
		printw(" %s %s", muted ? "🔇" : "", it->second.m_ChannelsLocked ? "🔒" : "");

		//append sinkname
		int      px = 0, py = 0;
		unsigned space = 0;
		getyx(stdscr, py, px);
		space = COLS - px - 3;

		std::string sinkname = interface->getSinkInfo()[it->second.m_Sink].m_Name;
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

	numDisplayedInputs = index - skipInputs;

	interface->modifyUnlock();
	refresh();
}

void updateMonitors(PAInterface *interface)
{
	std::lock_guard<std::mutex> lg(screenMutex);
	interface->modifyLock();
	iter_inputinfo_t it    = std::next(interface->getInputInfo().begin(), skipInputs);
	uint32_t         index = 0;
	for (; it != interface->getInputInfo().end(); it++, index++)
	{
		if (index >= skipInputs + numDisplayedInputs)
			break;
		uint32_t y = mapMonitorLines[it->first];
		generateMeter(y, 1, COLS - 2, it->second.m_Peak, 1.0);
	}
	interface->modifyUnlock();
	refresh();
}

inline iter_inputinfo_t get_selected_input_iter(PAInterface *interface)
{
	return std::next(interface->getInputInfo().begin(), selectedInput);
}

void change_volume(double pctDelta, PAInterface *interface)
{
	iter_inputinfo_t it = get_selected_input_iter(interface);
	interface->addVolume(it->first, it->second.m_ChannelsLocked ? -1 : selectedChannel, pctDelta);
}

void change_sink(bool increment, PAInterface *interface)
{
	iter_inputinfo_t iit = get_selected_input_iter(interface);
	iter_sinkinfo_t  sit = interface->getSinkInfo().find(iit->second.m_Sink);

	if (increment)
		sit++;
	else
	{
		if (sit == interface->getSinkInfo().begin())
			sit = std::next(sit, interface->getSinkInfo().size() - 1);
		else
			sit--;
	}

	if (sit == interface->getSinkInfo().end())
		sit = interface->getSinkInfo().begin();

	interface->setInputSink(iit->first, sit->first);
}

void mute_input(PAInterface *interface)
{
	iter_inputinfo_t it = get_selected_input_iter(interface);
	interface->setMute(it->first, !it->second.m_Mute);
}

void adjustDisplay()
{
	if (selectedInput >= skipInputs && selectedInput < skipInputs + numDisplayedInputs)
		return;
	if (selectedInput < skipInputs)
	{
		// scroll up until selected is at top
		skipInputs = selectedInput;
	}
	else
	{
		// scroll down until selected is at bottom
		uint32_t linesToFree = 0;
		uint32_t idx         = skipInputs + numDisplayedInputs;
		for (; idx <= selectedInput; idx++)
			linesToFree += mapIndexSize[idx]; // +1 ?

		uint32_t linesFreed = 0;
		idx                 = 0;
		while (linesFreed < linesToFree)
			linesFreed += mapIndexSize[idx++];
		skipInputs = idx;
	}
}

void selectNext(PAInterface *interface, bool channelLevel = true)
{
	if (selectedInput < interface->getInputInfo().size())
	{
		if (channelLevel)
		{
			iter_inputinfo_t it = get_selected_input_iter(interface);
			if (!it->second.m_ChannelsLocked && selectedChannel < it->second.m_PAVolume.channels - 1)
			{
				selectedChannel++;
				return;
			}
		}
		selectedInput++;
		if (selectedInput >= interface->getInputInfo().size())
			selectedInput = interface->getInputInfo().size() - 1;
		else
			selectedChannel = 0;
	}
	adjustDisplay();
}

void selectPrev(PAInterface *interface, bool channelLevel = true)
{
	if (selectedInput < interface->getInputInfo().size())
	{
		if (channelLevel)
		{
			iter_inputinfo_t it = get_selected_input_iter(interface);
			if (!it->second.m_ChannelsLocked && selectedChannel > 0)
			{
				selectedChannel--;
				return;
			}
		}
		if (selectedInput > 0)
			selectedInput--;
		else if (!get_selected_input_iter(interface)->second.m_ChannelsLocked)
		{
			selectedChannel = get_selected_input_iter(interface)->second.m_PAVolume.channels - 1;
		}
	}
	adjustDisplay();
}

void toggleChannelLock(PAInterface *interface)
{
	iter_inputinfo_t it         = get_selected_input_iter(interface);
	it->second.m_ChannelsLocked = !it->second.m_ChannelsLocked;
}

void inputThread(PAInterface *interface)
{
	while (running)
	{
		int ch = getch();
		switch (ch)
		{
		case 'h':
			change_volume(-0.05, interface);
			break;
		case 'H':
			change_volume(-0.15, interface);
			break;
		case 'l':
			change_volume(0.05, interface);
			break;
		case 'L':
			change_volume(0.15, interface);
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
			change_sink(true, interface);
			break;
		case 'S':
			change_sink(false, interface);
			break;
		case 'm':
			mute_input(interface);
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
	bool updAll = type & (PAI_SUBSCRIPTION_MASK_INPUT | PAI_SUBSCRIPTION_MASK_SINK);
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

int main(int argc, char **argv)
{
	setlocale(LC_ALL, "");
	initscr();
	init_colors();
	curs_set(0); // make cursor invisible
	noecho();

	signal(SIGWINCH, sig_handle_resize);

	PAInterface pai("pamix");
	pai.subscribe(pai_subscription);

	std::thread inputT(inputThread, &pai);
	inputT.detach();

	signal_update(true);
	while (running)
	{
		std::unique_lock<std::mutex> lk(updMutex);
		cv.wait(lk, [] { return !updateDataQ.empty(); });

		if (updateDataQ.front().redrawAll)
		{
			updatesinks(&pai);
		}
		else
		{
			updateMonitors(&pai);
		}
		updateDataQ.pop();
	}
	endwin();
	return 0;
}
