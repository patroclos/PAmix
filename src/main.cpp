#include <queue>
#include <condition_variable>
#include <mutex>
#include <ncurses.h>
#include <painterface.h>
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

//debug vars
int numSignals = 0;
int numWaits   = 0;

std::map<uint32_t, unsigned> mapMonitorLines;

// sync main and callback threads
std::mutex              updMutex;
std::condition_variable cv;

struct UpdateData
{
	bool redrawAll;
	UpdateData()=default;
	UpdateData(bool redrawAll){this->redrawAll=redrawAll;}
};

std::queue<UpdateData> updateDataQ;

void signal_update(bool all)
{
	{
		std::lock_guard<std::mutex> lk(updMutex);
		updateDataQ.push(UpdateData(all));
		numSignals++;
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
	printw("selected: %d redraws: %d numSinkInfo: %d numInputInfo: %d", selected, numRedraws, interface->getSinkInfo().size(), interface->getInputInfo().size());
	printw("Waits: %d, Sigs: %d", numWaits, numSignals);

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
		std::string descline = "%s  %.2fdB %.2f%%";
		if (selected == index++)
			descline.insert(0, "--> ");

		mvprintw(y++, 1, descline.c_str(), appname.c_str(), dB, vol * 100);
		y += 3;
	}

#if 0
	//display monitors
	for (iter_inputinfo_t it = interface->getInputInfo().begin(); it != interface->getInputInfo().end(); it++)
	{
		mvprintw(y++, 1, "input: %d monitorstream: %p", it->first, it->second.m_Monitor);
	}
#endif

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
	for (int i = 0; i < selected; i++)
		it++;
	interface->addVolume(it->first, pctDelta);
}

void inputThread(PAInterface *interface)
{
	while (running)
	{
		bool sig = false;
		int  ch  = getch();
		switch (ch)
		{
		case 'h':
			change_volume(-0.05, interface);
			break;
		case 'l':
			change_volume(0.05, interface);
			break;
		case 'j':
			selected = (selected + 1) % interface->getInputInfo().size();
			sig      = true;
			break;
		case 'k':
			selected = (selected - 1) % interface->getInputInfo().size();
			sig      = true;
			break;
		case 'q':
			running = false;
			sig     = true;
			break;
		default:
			break;
		}

		if (sig)
		{
			signal_update(true);
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
		numWaits++;

		for (iter_inputinfo_t it = pai.getInputInfo().begin(); it != pai.getInputInfo().end(); it++)
			if (!it->second.m_Monitor)
				pai.createMonitorStreamForSinkInput(it);

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
