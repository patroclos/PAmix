#pragma once

#include <assert.h>
#include <cstring>
#include <ctgmath>
#include <map>
#include <pulse/pulseaudio.h>
#include <thread>

struct mainloop_lockguard
{
	pa_threaded_mainloop *m;
	mainloop_lockguard(pa_threaded_mainloop *m);
	~mainloop_lockguard();
};

struct InputInfo
{
	std::string m_Appname;
	uint32_t    m_Sink;
	uint8_t     m_Channels;
	pa_stream * m_Monitor;
	double      m_Peak;
	pa_volume_t m_Volume;
	bool        m_Kill;

	InputInfo(const pa_sink_input_info *info);
	InputInfo() = default;
	~InputInfo();

	void update(const pa_sink_input_info *info);
};

struct SinkInfo
{
	uint32_t m_MonitorSource;

	SinkInfo(const pa_sink_info *info);
	SinkInfo() = default;
};

typedef std::map<uint32_t, InputInfo>::iterator iter_inputinfo_t;
typedef std::map<uint32_t, SinkInfo>::iterator  iter_sinkinfo_t;

//define subscription masks
#define PAI_SUBSCRIPTION_MASK_PEAK 0x1U
#define PAI_SUBSCRIPTION_MASK_INPUT 0x2U
#define PAI_SUBSCRIPTION_MASK_SINK 0x4U
#define PAI_SUBSCRIPTION_MASK_OTHER 0x8U

typedef uint32_t pai_subscription_type_t;

class PAInterface;
typedef void (*pai_subscription_cb)(PAInterface *, const pai_subscription_type_t);

class PAInterface
{
private:
	pa_threaded_mainloop *m_Mainloop;
	pa_mainloop_api *     m_MainloopApi;
	pa_context *          m_Context;

	std::map<uint32_t, InputInfo> m_Sinkinputinfos;
	std::map<uint32_t, SinkInfo>  m_Sinkinfos;

	pai_subscription_cb m_Subscription_callback;

private:
	static void signal_mainloop(void *interface);
	//PulseAudio API Callbacks
	//userptr points to current PAInterface instance
	static void cb_context_state(pa_context *context, void *interface);
	static void cb_success(pa_context *context, int success, void *interface);
	static void cb_subscription_event(pa_context *context, pa_subscription_event_type_t type, uint32_t idx, void *interface);
	static void cb_sink_input_info(pa_context *context, const pa_sink_input_info *info, int eol, void *interface);
	static void cb_sink_info(pa_context *context, const pa_sink_info *info, int eol, void *interface);
	static void cb_read(pa_stream *stream, size_t nbytes, void *interface);
	static void cb_stream_state(pa_stream *stream, void *inputinfo);

	static void _updateInputs(PAInterface *interface);
	static void _updateSinks(PAInterface *interface);

	//member methods

	void updateInputs();
	void updateSinks();
	void notifySubscription(const pai_subscription_type_t);
	int createMonitorStreamForSinkInput(iter_inputinfo_t &iiiter);

public:
	PAInterface(const char *context_name);
	~PAInterface();

	void subscribe(pai_subscription_cb callback);

	std::map<uint32_t, InputInfo> &getInputInfo();

	std::map<uint32_t, SinkInfo> &getSinkInfo();

	void addVolume(const uint32_t inputidx, const double pctDelta);
};
