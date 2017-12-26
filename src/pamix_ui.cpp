#include <pamix_ui.hpp>

#ifdef FEAT_UNICODE

#include <ncursesw/ncurses.h>

#else
#include <ncurses.h>
#endif

#include <pamix.hpp>

void pamix_ui::reset() {
	m_Entries = nullptr;
	m_VolumeBarLineNums.clear();
	m_EntrySizes.clear();
	m_NumDrawnEntries = 0;
	m_NumSkippedEntries = 0;
}

void pamix_ui::drawVolumeBar(int y, int x, int width, double fill, double maxFill) const {

	int segments = width - 2;

	if (segments <= 0)
		return;

	if (fill < 0)
		fill = 0;
	else if (fill > maxFill)
		fill = maxFill;

	auto filled = (unsigned) (fill / maxFill * (double) segments);
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

void string_maxlen_abs(std::string &str, unsigned max) {
	if (str.length() > max) {
		str = str.substr(0, max - 2);
		str.append("..");
	}
}

void string_maxlen_pct(std::string &str, double maxPct) {
	string_maxlen_abs(str, (unsigned) (COLS * maxPct));
}

void pamix_ui::redrawAll() {
	std::lock_guard<std::mutex> lockGuard(m_DrawMutex);

	clear();
	drawHeader();

	unsigned lineNumber = 2;
	unsigned entryIndex = 0;

	auto entryIter = m_Entries->begin();
	for (auto end = m_Entries->end(); entryIter != end; entryIter++, entryIndex++)
		m_EntrySizes[entryIndex] = entryIter->second->m_Lock ? (char) 1 : entryIter->second->m_PAVolume.channels;

	entryIndex = m_NumSkippedEntries;
	entryIter = std::next(m_Entries->begin(), entryIndex);

	for (auto end = m_Entries->end(); entryIter != end; entryIter++, entryIndex++) {
		Entry *entry = entryIter->second.get();

		std::string applicationName = entryIter->second ? entry->m_Name : "";
		pa_volume_t averageVolume = pa_cvolume_avg(&entry->m_PAVolume);
		char numChannels = entry->m_Lock ? (char) 1 : entry->m_PAVolume.channels;
		bool isSelectedEntry = entryIndex == m_SelectedEntry;

		if (lineNumber + numChannels + 2 > (unsigned) LINES)
			break;

		lineNumber = drawEntryControlMeters(entry, entryIndex, lineNumber);

		double volumePeak = entry->m_Peak;
		m_VolumeBarLineNums[entryIter->first] = lineNumber;
		if (entry->m_Meter)
			drawVolumeBar(lineNumber++, 1, COLS - 2, volumePeak, 1.0);

		string_maxlen_pct(applicationName, 0.4);
		if (isSelectedEntry)
			attron(A_STANDOUT);
		mvprintw(lineNumber++, 1, applicationName.c_str());
		attroff(A_STANDOUT);

		bool isMuted = entry->m_Mute || averageVolume == PA_VOLUME_MUTED;
		printw(" %s %s", isMuted ? SYM_MUTE : "", entry->m_Lock ? SYM_LOCK : "");

		int curX = 0, curY = 0;
		getyx(stdscr, curY, curX);
		unsigned remainingChars = (unsigned) COLS - curX - 3;

		std::string displayName = getEntryDisplayName(entry);
		if (remainingChars < displayName.length()) {
			string_maxlen_abs(displayName, remainingChars);
			remainingChars = 0;
		}
		mvprintw(curY, curX + remainingChars + 1, displayName.c_str());
		lineNumber++;
	}

	m_NumDrawnEntries = entryIndex - m_NumSkippedEntries;
	refresh();
}

unsigned int pamix_ui::drawEntryControlMeters(const Entry *entry, unsigned entryIndex, unsigned int lineNumber) const {
	pa_volume_t averageVolume = pa_cvolume_avg(&entry->m_PAVolume);
	double dB = pa_sw_volume_to_dB(averageVolume);
	double vol = averageVolume / (double) PA_VOLUME_NORM;
	char numChannels = entry->m_Lock ? (char) 1 : entry->m_PAVolume.channels;
	bool isSelectedEntry = entryIndex == m_SelectedEntry;
	if (entry->m_Meter) {
		if (entry->m_Lock) {
			drawVolumeBar(lineNumber, 32, COLS - 33, vol, MAX_VOL);

			std::string descriptionTemplate = "%.2fdB (%.2f)";
			if (isSelectedEntry)
				descriptionTemplate.insert(0, SYM_ARROW);
			mvprintw(lineNumber++, 1, descriptionTemplate.c_str(), dB, vol);
		} else {
			for (char channel = 0; channel < numChannels; channel++) {
				std::string descriptionTemplate = "%.*s %.2fdB (%.2f)";

				uint32_t volume = entry->m_PAVolume.values[channel];
				bool isSelectedChannel = isSelectedEntry && channel == m_SelectedChannel;
				double channel_dB = pa_sw_volume_to_dB(volume);
				double channel_pct = volume / (double) MAX_VOL;
				pa_channel_position_t channelPosition = entry->m_PAChannelMap.map[channel];
				std::string channelPrettyName = pa_channel_position_to_pretty_string(channelPosition);

				drawVolumeBar(lineNumber, 32, COLS - 33, channel_pct, MAX_VOL);
				if (isSelectedChannel)
					descriptionTemplate.insert(0, SYM_ARROW);

				unsigned indent = isSelectedChannel ? 13 : 15;
				mvprintw(lineNumber++, 1, descriptionTemplate.c_str(), indent, channel_dB, channel_pct);
			}
		}
	}
	return lineNumber;
}

void pamix_ui::redrawVolumeBars() {
	std::lock_guard<std::mutex> lockGuard(m_DrawMutex);

	auto it = std::next(m_Entries->begin(), m_NumSkippedEntries);
	uint32_t index = 0;
	for (auto end = m_Entries->end(); it != end; it++, index++) {
		if (index >= m_NumSkippedEntries + m_NumDrawnEntries)
			break;
		uint32_t y = m_VolumeBarLineNums[it->first];
		if (it->second->m_Meter)
			drawVolumeBar(y, 1, COLS - 2, it->second->m_Peak, 1.0);
	}
}

void pamix_ui::drawHeader() const {
	mvprintw(0, 1, "%d/%d", m_Entries->empty() ? 0 : m_SelectedEntry + 1, m_Entries->size());
	mvprintw(0, 10, "%s", entryTypeNames[m_EntriesType]);
}

std::string pamix_ui::getEntryDisplayName(Entry *entry) {
	switch (m_EntriesType) {
		case ENTRY_SINK: {
			return ((SinkEntry *) entry)->getPort();
		}
		case ENTRY_SOURCE: {
			return ((SourceEntry *) entry)->getPort();
		}
		case ENTRY_SINKINPUT: {
			auto sinkInput = (SinkInputEntry *) entry;
			return m_paInterface->getSinks()[sinkInput->m_Device]->m_Name;
		}
		case ENTRY_SOURCEOUTPUT: {
			auto sourceOutput = (SourceOutputEntry *) entry;
			return m_paInterface->getSources()[sourceOutput->m_Device]->m_Name;
		}
		case ENTRY_CARDS: {
			return ((CardEntry *) entry)->m_Profiles[((CardEntry *) entry)->m_Profile].description;
		}
		default:
			return "UNKNOWN ENTRY TYPE";
	}
}

pamix_ui::pamix_ui(PAInterface *paInterface) : m_paInterface(paInterface) {

}

