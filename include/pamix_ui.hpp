#pragma once

#include <cstdint>
#include <map>
#include "entry.hpp"
#include "painterface.hpp"

class pamix_ui {
private:
	std::map<uint32_t, unsigned> m_VolumeBarLineNums;
	std::map<uint32_t, uint8_t> m_EntrySizes;
	unsigned m_NumDrawnEntries;
	std::mutex m_DrawMutex;
public:
	unsigned m_SelectedEntry;
	unsigned m_SelectedChannel;
	unsigned m_NumSkippedEntries;
	std::map<uint32_t, std::unique_ptr<Entry>> *m_Entries;
	entry_type m_EntriesType;
	PAInterface *m_paInterface;
public:
	explicit pamix_ui(PAInterface *paInterface);
	void reset();

	void selectEntries(entry_type type);

	void redrawAll();

	void redrawVolumeBars();

	std::string getEntryDisplayName(Entry *entry);

	pamix_entry_iter_t getSelectedEntryIterator();

	void selectNext(bool includeChannels);
	void selectPrevious(bool includeChannels);

	int getKeyInput();

private:
	void drawHeader() const;

	void drawVolumeBar(int y, int x, int width, double fill, double maxFill) const;

	unsigned int drawEntryControlMeters(const Entry *entry, unsigned int entryIndex, unsigned int lineNumber) const;

	void adjustDisplayedEntries();

	void moveSelection(int delta, bool includeChannels);
};
