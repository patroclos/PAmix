#include <condition_variable>
#include <locale.h>
#include <mutex>
#include <ncursesw/ncurses.h>
#include <painterface.h>
#include <queue>
#include <signal.h>
#include <string>
#include <thread>

struct VolBarData
{
	unsigned m_Y;
	unsigned m_X;
	unsigned m_Width;
	VolBarData(unsigned y, unsigned x, unsigned w) : m_Y(y), m_X(x), m_Width(w)
	{
	}
	VolBarData() = default;
};

struct UpdateData
{
	bool redrawAll;
	UpdateData() = default;
	UpdateData(bool redrawAll) { this->redrawAll = redrawAll; }
};

#define DECAY_STEP 0.04
#define MAX_VOL 1.5
#define SYM_VOLBAR L'\u25ae' //▮

// GLOBAL VARIABLES
bool     running         = true;
unsigned selectedInput   = 0;
uint8_t  selectedChannel = 0;
std::map<uint32_t, double> lastPeaks;
std::mutex screenMutex;

std::map<uint32_t, VolBarData> mapMonitorLines;

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

	std::wstring meter = L"[]";

	meter.insert(1, filled, SYM_VOLBAR);
	meter.insert(filled + 1, segments - filled, ' ');
	mvaddwstr(y, x, meter.c_str());
}

void updatesinks(PAInterface *interface)
{
	//avoid concurrent drawing
	std::lock_guard<std::mutex> lg(screenMutex);

	if (selectedInput > interface->getInputInfo().size() - 1)
	{
		selectedInput   = interface->getInputInfo().size() - 1;
		selectedChannel = 0;
	}

	clear();

	unsigned y     = 1;
	unsigned index = 0;

	for (iter_inputinfo_t it = interface->getInputInfo().begin(); it != interface->getInputInfo().end(); it++)
	{
		std::string &appname = it->second.m_Appname;
		pa_volume_t  avgvol  = it->second.getAverageVolume();
		double       dB      = pa_sw_volume_to_dB(avgvol);
		double       vol     = avgvol / (double)PA_VOLUME_NORM;

		bool isSelInput = index++ == selectedInput;

		if (it->second.m_ChannelsLocked)
		{
			generateMeter(y, 32, COLS - 33, vol, MAX_VOL);

			std::string descstring = "%.2fdB (%.2f)";
			if(isSelInput)descstring.insert(0,"▶ ");
			mvprintw(y++, 1, descstring.c_str(), dB, vol);
		}
		else
		{
			for (uint32_t chan = 0; chan < it->second.m_PAVolume.channels; chan++)
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

		mapMonitorLines[it->first] = VolBarData(y, 1, COLS - 2);
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

	refresh();
}

void updateMonitors(PAInterface *interface)
{
	std::lock_guard<std::mutex> lg(screenMutex);
	for (iter_inputinfo_t it = interface->getInputInfo().begin(); it != interface->getInputInfo().end(); it++)
	{
		VolBarData vbd = mapMonitorLines[it->first];
		generateMeter(vbd.m_Y, vbd.m_X, vbd.m_Width, it->second.m_Peak, 1.0);
	}
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

int main(int argc, char **argv)
{
	setlocale(LC_ALL, "");
	initscr();
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
