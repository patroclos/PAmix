#pragma once

#include <painterface.hpp>
#include <pulse/pulseaudio.h>
#include <string>
#include <vector>
#include <map>
#include <volumeutil.hpp>

class PAInterface;

enum entry_type {
	ENTRY_SINK,
	ENTRY_SOURCE,
	ENTRY_SINKINPUT,
	ENTRY_SOURCEOUTPUT,
	ENTRY_CARDS,
	ENTRY_COUNT
};

static std::map<entry_type, const char *> entryTypeNames = {
		{ENTRY_SINK,         "Output Devices"},
		{ENTRY_SINKINPUT,    "Playback"},
		{ENTRY_SOURCE,       "Input Devices"},
		{ENTRY_SOURCEOUTPUT, "Recording"},
		{ENTRY_CARDS,        "Cards"}
};

struct Entry {
	PAInterface *interface;
	std::string m_Name;
	uint32_t m_Index;
	double m_Peak = 0;
	pa_stream *m_Monitor = nullptr;
	uint32_t m_MonitorIndex;
	bool m_Mute;
	pa_cvolume m_PAVolume;
	pa_channel_map m_PAChannelMap;
	bool m_Lock = true;
	bool m_Kill;
	bool m_Meter = true;

	Entry(PAInterface *iface);

	~Entry();

	// general methods
	virtual void setVolume(const int channel, const pa_volume_t volume) = 0;

	virtual void setMute(bool mute) = 0;

	virtual void addVolume(const int channel, const double deltaPct);

	virtual void cycleSwitch(bool increment) = 0;

	virtual void update(const pa_sink_info *info) {}

	virtual void update(const pa_source_info *info) {}

	virtual void update(const pa_sink_input_info *info) {}

	virtual void update(const pa_source_output_info *info) {}

	virtual void update(const pa_card_info *info) {}

	// device methods
	virtual void suspend() {}

	virtual void setPort(const char *port) {}

	// stream methods
	virtual void move(uint32_t idx) {}

	virtual void kill() {}
};

struct DeviceEntry : public Entry {
	int m_Port;
	std::vector<std::string> m_Ports;

	DeviceEntry(PAInterface *iface)
			: Entry(iface) {};

	// general methods
	virtual void setVolume(const int channel, const pa_volume_t volume) = 0;

	virtual void setMute(bool mute)          = 0;

	virtual void cycleSwitch(bool increment) = 0;

	virtual std::string getPort() { return m_Port > -1 ? m_Ports[m_Port] : ""; }

	virtual void setPort(const char *port) = 0;
};

struct StreamEntry : public Entry {
	uint32_t m_Device;

	StreamEntry(PAInterface *iface)
			: Entry(iface) {};

	// general methods
	virtual void setVolume(const int channel, const pa_volume_t volume) = 0;

	virtual void setMute(bool mute)          = 0;

	virtual void cycleSwitch(bool increment) = 0;

	virtual void move(uint32_t idx) = 0;

	virtual void kill()             = 0;
};

struct SinkEntry : public DeviceEntry {
	pa_sink_state_t m_State;

	SinkEntry(PAInterface *iface)
			: DeviceEntry(iface) {}

	void update(const pa_sink_info *info);

	// general methods
	virtual void setVolume(const int channel, const pa_volume_t volume);

	virtual void setMute(bool mute);

	virtual void cycleSwitch(bool increment);

	virtual void setPort(const char *port);
};

struct SourceEntry : public DeviceEntry {
	pa_source_state_t m_State;

	SourceEntry(PAInterface *iface)
			: DeviceEntry(iface) {}

	void update(const pa_source_info *info);

	// general methods
	virtual void setVolume(const int channel, const pa_volume_t volume);

	virtual void setMute(bool mute);

	virtual void cycleSwitch(bool increment);

	virtual void setPort(const char *port);
};

struct SinkInputEntry : public StreamEntry {
	void update(const pa_sink_input_info *info);

	SinkInputEntry(PAInterface *iface)
			: StreamEntry(iface) {}

	// general methods
	virtual void setVolume(const int channel, const pa_volume_t volume);

	virtual void setMute(bool mute);

	virtual void cycleSwitch(bool increment);

	virtual void move(uint32_t idx);

	virtual void kill();
};

struct SourceOutputEntry : public StreamEntry {
	void update(const pa_source_output_info *info);

	SourceOutputEntry(PAInterface *iface)
			: StreamEntry(iface) {}

	// general methods
	virtual void setVolume(const int channel, const pa_volume_t volume);

	virtual void setMute(bool mute);

	virtual void cycleSwitch(bool increment);

	virtual void move(uint32_t idx);

	virtual void kill();
};


struct CardEntry : public Entry {
	struct CardProfile {
		std::string name;
		std::string description;

		CardProfile(pa_card_profile_info2 *profile)
				: name(profile->name), description(profile->description) {};
	};

	int m_Profile;
	std::vector<CardProfile> m_Profiles;

	void update(const pa_card_info *info);

	CardEntry(PAInterface *iface)
			: Entry(iface) {}

	virtual void cycleSwitch(bool increment);

	virtual void setVolume(const int channel, const pa_volume_t volume) {}

	virtual void setMute(bool mute) {}

	virtual void move(uint32_t idx) {}

	virtual void kill() {}
};
