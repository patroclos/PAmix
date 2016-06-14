#include <locale.h>
#include <condition_variable>
#include <mutex>
#include <ncurses.h>
#include <painterface.h>
#include <queue>
#include <signal.h>
#include <string>
#include <thread>

// GLOBAL VARIABLES
bool     running    = true;
int      selected   = -1;
unsigned numRedraws = 0;
std::map<uint32_t, double> lastPeaks;
#define DECAY_STEP 0.04
std::mutex screenMutex;

std::map<uint32_t, unsigned> mapMonitorLines;

// sync main and callback threads
std::mutex              updMutex;
std::condition_variable cv;

struct UpdateData
{
	bool redrawAll;
	UpdateData() = default;
	UpdateData(bool redrawAll) { this->redrawAll = redrawAll; }
};

std::queue<UpdateData> updateDataQ;

void signal_update(bool all)
{
	{
		std::lock_guard<std::mutex> lk(updMutex);
		updateDataQ.push(UpdateData(all));
	}
	cv.notify_one();
}

std::string generateVolumeMeter(double vol, unsigned padding = 1)
{
	double      maxvol   = 1.5;
	std::string meter    = "[]";
	int         segments = COLS - 2 * padding - 2;
	int         filled   = vol / maxvol * (double)segments;
	meter.insert(1, filled, '#');
	meter.insert(filled + 1, segments - filled, ' ');
	return meter;
}

void updatesinks(PAInterface *interface)
{
	//avoid concurrent drawing
	std::lock_guard<std::mutex> lg(screenMutex);

	numRedraws++;

	if (selected == -1 && !interface->getInputInfo().empty())
		selected = 0;

	clear();
	printw("Selected SinkInput: %d", selected);

	unsigned y     = 3;
	int      index = 0;

	for (iter_inputinfo_t it = interface->getInputInfo().begin(); it != interface->getInputInfo().end(); it++)
	{
		std::string &appname = it->second.m_Appname;
		pa_volume_t  avgvol  = it->second.m_Volume;
		double       dB      = pa_sw_volume_to_dB(avgvol);
		double       vol     = avgvol / (double)PA_VOLUME_NORM;

		std::string volbar = generateVolumeMeter(vol);

		mvprintw(y++, 1, volbar.c_str());

		double peak = it->second.m_Peak;

		std::string peakbar = generateVolumeMeter(peak);

		mapMonitorLines[it->first] = y;
		mvprintw(y++, 1, peakbar.c_str());

		//mark selected input with arrow in front of name
		std::string descline = "%s  %.2fdB %.2f%% %s";
		if (selected == index++)
			descline.insert(0, "▶ ");

		mvprintw(y++, 1, descline.c_str(), appname.c_str(), dB, vol * 100, it->second.m_Mute ? "🔇" : "");
		//append sinkname
		int px = 0, py = 0;
		int space = 0;
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
		std::string bar = generateVolumeMeter(it->second.m_Peak);
		mvprintw(mapMonitorLines[it->first], 1, bar.c_str());
	}
	refresh();
}

void change_volume(double pctDelta, PAInterface *interface)
{
	iter_inputinfo_t it = interface->getInputInfo().begin();
	std::advance(it, selected);
	interface->addVolume(it->first, pctDelta);
}

void change_sink(bool increment, PAInterface *interface)
{
	iter_inputinfo_t iit = interface->getInputInfo().begin();
	std::advance(iit, selected);
	iter_sinkinfo_t sit = interface->getSinkInfo().find(iit->second.m_Sink);

	if (increment)
		sit++;
	else
	{
		if(sit==interface->getSinkInfo().begin())
			sit=std::next(sit, interface->getSinkInfo().size()-1);
		else
			sit--;
	}

	if (sit == interface->getSinkInfo().end())
		sit = interface->getSinkInfo().begin();

	interface->setInputSink(iit->first, sit->first);
}

void mute_input(PAInterface *interface)
{
	iter_inputinfo_t it = std::next(interface->getInputInfo().begin(), selected);
	interface->setMute(it->first, !it->second.m_Mute);
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
			selected = (selected + 1) % interface->getInputInfo().size();
			signal_update(true);
			break;
		case 'k':
			selected = (selected - 1) % interface->getInputInfo().size();
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
	signal_update(type & (PAI_SUBSCRIPTION_MASK_INPUT | PAI_SUBSCRIPTION_MASK_SINK));
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

	//handle terminal resize
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
