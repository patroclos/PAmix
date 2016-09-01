#pragma once

#include <assert.h>
#include <cstring>
#include <ctgmath>
#include <entry.hpp>
#include <map>
#include <mutex>
#include <pulse/pulseaudio.h>
#include <thread>
#include <vector>

struct mainloop_lockguard
{
	pa_threaded_mainloop *m;
	mainloop_lockguard(pa_threaded_mainloop *m);
	~mainloop_lockguard();
};

class PAInterface;
struct Entry;

//define subscription masks
#define PAI_SUBSCRIPTION_MASK_PEAK 0x1U
#define PAI_SUBSCRIPTION_MASK_INFO 0x2U
#define PAI_SUBSCRIPTION_MASK_OTHER 0x4U

typedef uint32_t pai_subscription_type_t;

typedef void (*pai_subscription_cb)(PAInterface *, const pai_subscription_type_t);
typedef std::map<uint32_t, std::unique_ptr<Entry>>::iterator iter_entry_t;

class PAInterface
{
private:
	bool m_Autospawn;

	const char *          m_ContextName;
	pa_threaded_mainloop *m_Mainloop;
	pa_mainloop_api *     m_MainloopApi;
	pa_context *          m_Context;

	std::map<uint32_t, std::unique_ptr<Entry>> m_Sinks;
	std::map<uint32_t, std::unique_ptr<Entry>> m_Sources;
	std::map<uint32_t, std::unique_ptr<Entry>> m_SinkInputs;
	std::map<uint32_t, std::unique_ptr<Entry>> m_SourceOutputs;

	std::mutex m_modifyMutex;

	pai_subscription_cb m_Subscription_callback;

private:
	static void signal_mainloop(void *interface);

	static void _updateSinks(PAInterface *interface);
	static void _updateSources(PAInterface *interface);
	static void _updateInputs(PAInterface *interface);
	static void _updateOutputs(PAInterface *interface);

	static void _updatethread(pai_subscription_type_t paisubtype, pa_subscription_event_type_t type, PAInterface *interface);

	//member methods

	void updateSinks() { _updateSinks(this); }
	void updateSources() { _updateSources(this); }
	void updateInputs() { _updateInputs(this); }
	void updateOutputs() { _updateOutputs(this); }

	void notifySubscription(const pai_subscription_type_t);

public:
	PAInterface(const char *context_name);
	~PAInterface();

	inline pa_threaded_mainloop *getPAMainloop() { return m_Mainloop; }
	inline pa_context *          getPAContext() { return m_Context; }

	bool connect();
	bool isConnected();

	void setAutospawn(bool as) { m_Autospawn = as; }
	bool                   getAutospawn() { return m_Autospawn; }

	void subscribe(pai_subscription_cb callback);

	std::map<uint32_t, std::unique_ptr<Entry>> &getSinks() { return m_Sinks; }
	std::map<uint32_t, std::unique_ptr<Entry>> &getSources() { return m_Sources; }
	std::map<uint32_t, std::unique_ptr<Entry>> &getSinkInputs() { return m_SinkInputs; }
	std::map<uint32_t, std::unique_ptr<Entry>> &getSourceOutputs() { return m_SourceOutputs; }

	std::vector<std::unique_ptr<std::pair<PAInterface *, Entry *>>> m_IEPairs;
	void createMonitorStreamForEntry(Entry *entry, int type);

	void modifyLock();
	void modifyUnlock();

	//PulseAudio API Callbacks
	//userptr points to current PAInterface instance
	static void cb_context_state(pa_context *context, void *interface);
	static void cb_context_drain_complete(pa_context *context, void *null);
	static void cb_success(pa_context *context, int success, void *interface);
	static void cb_subscription_event(pa_context *context, pa_subscription_event_type_t type, uint32_t idx, void *interface);

	static void cb_sink_info(pa_context *context, const pa_sink_info *info, int eol, void *interface);
	static void cb_source_info(pa_context *context, const pa_source_info *info, int eol, void *interface);
	static void cb_sink_input_info(pa_context *context, const pa_sink_input_info *info, int eol, void *interface);
	static void cb_source_output_info(pa_context *context, const pa_source_output_info *info, int eol, void *interface);

	static void cb_read(pa_stream *stream, size_t nbytes, void *iepair);
	static void cb_stream_state(pa_stream *stream, void *entry);
};
