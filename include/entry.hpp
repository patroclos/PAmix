#pragma once

#include <painterface.hpp>
#include <pulse/pulseaudio.h>
#include <string>
#include <vector>
#include <volumeutil.hpp>

class PAInterface;

enum entry_type
{
	ENTRY_SINK,
	ENTRY_SOURCE,
	ENTRY_SINKINPUT,
	ENTRY_SOURCEOUTPUT
};

struct Entry
{
	std::string    m_Name;
	uint32_t       m_Index;
	double         m_Peak;
	pa_stream *    m_Monitor = nullptr;
	uint32_t       m_MonitorIndex;
	bool           m_Mute;
	pa_cvolume     m_PAVolume;
	pa_channel_map m_PAChannelMap;
	bool           m_Lock = true;
	bool           m_Kill;

	// general methods
	virtual void setVolume(PAInterface *interface, const int channel, const pa_volume_t volume) = 0;
	virtual void setMute(PAInterface *interface, bool mute) = 0;
	virtual void addVolume(PAInterface *interface, const int channel, const double deltaPct)
	{
		volume_pct_delta(&m_PAVolume, channel, deltaPct);
		setVolume(interface, channel, m_Lock ? pa_cvolume_avg(&m_PAVolume) : m_PAVolume.values[channel]);
	}
	virtual void cycleSwitch(PAInterface *interface, bool increment) = 0;

	virtual void update(const pa_sink_info *info) {}
	virtual void update(const pa_source_info *info) {}
	virtual void update(const pa_sink_input_info *info) {}
	virtual void update(const pa_source_output_info *info) {}

	// device methods
	virtual void suspend(PAInterface *interface) {}
	virtual void setPort(PAInterface *interface, const char *port) {}

	// stream methods
	virtual void move(PAInterface *interface, uint32_t idx) {}
	virtual void kill(PAInterface *interface) {}
};

struct DeviceEntry : public Entry
{
	int                      m_Port;
	std::vector<std::string> m_Ports;

	// general methods
	virtual void setVolume(PAInterface *interface, const int channel, const pa_volume_t volume) = 0;
	virtual void setMute(PAInterface *interface, bool mute)          = 0;
	virtual void cycleSwitch(PAInterface *interface, bool increment) = 0;

	virtual std::string getPort() { return m_Port > -1 ? m_Ports[m_Port] : ""; }
	virtual void setPort(PAInterface *interface, const char *port) = 0;
};

struct StreamEntry : public Entry
{
	uint32_t m_Device;

	// general methods
	virtual void setVolume(PAInterface *interface, const int channel, const pa_volume_t volume) = 0;
	virtual void setMute(PAInterface *interface, bool mute)          = 0;
	virtual void cycleSwitch(PAInterface *interface, bool increment) = 0;

	virtual void move(PAInterface *interface, uint32_t idx) = 0;
	virtual void kill(PAInterface *interface) = 0;
};

struct SinkEntry : public DeviceEntry
{
	pa_sink_state_t m_State;

	void update(const pa_sink_info *info);

	// general methods
	virtual void setVolume(PAInterface *interface, const int channel, const pa_volume_t volume);
	virtual void setMute(PAInterface *interface, bool mute);
	virtual void cycleSwitch(PAInterface *interface, bool increment);

	virtual void setPort(PAInterface *interface, const char *port);
};

struct SourceEntry : public DeviceEntry
{
	pa_source_state_t m_State;

	void update(const pa_source_info *info);

	// general methods
	virtual void setVolume(PAInterface *interface, const int channel, const pa_volume_t volume);
	virtual void setMute(PAInterface *interface, bool mute);
	virtual void cycleSwitch(PAInterface *interface, bool increment);

	virtual void setPort(PAInterface *interface, const char *port);
};

struct SinkInputEntry : public StreamEntry
{
	void update(const pa_sink_input_info *info);

	// general methods
	virtual void setVolume(PAInterface *interface, const int channel, const pa_volume_t volume);
	virtual void setMute(PAInterface *interface, bool mute);
	virtual void cycleSwitch(PAInterface *interface, bool increment);

	virtual void move(PAInterface *interface, uint32_t idx);
	virtual void kill(PAInterface *interface);
};

struct SourceOutputEntry : public StreamEntry
{
	void update(const pa_source_output_info *info);

	// general methods
	virtual void setVolume(PAInterface *interface, const int channel, const pa_volume_t volume);
	virtual void setMute(PAInterface *interface, bool mute);
	virtual void cycleSwitch(PAInterface *interface, bool increment);

	virtual void move(PAInterface *interface, uint32_t idx);
	virtual void kill(PAInterface *interface);
};
