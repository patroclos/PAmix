#include <iostream>
#include <painterface.h>

mainloop_lockguard::mainloop_lockguard(pa_threaded_mainloop *m) : m(m)
{
	pa_threaded_mainloop_lock(m);
}

mainloop_lockguard::~mainloop_lockguard()
{
	pa_threaded_mainloop_unlock(m);
}

InputInfo::InputInfo(const pa_sink_input_info *info)
{
	update(info);
	m_Peak    = 0.0;
	m_Monitor = nullptr;
}

InputInfo::~InputInfo()
{
	if (m_Monitor)
	{
		pa_stream_disconnect(m_Monitor);
		pa_stream_unref(m_Monitor);
	}
}

SinkInfo::SinkInfo(const pa_sink_info *info)
{
	m_MonitorSource = info->monitor_source;
}

void InputInfo::update(const pa_sink_input_info *info)
{
	m_Sink     = info->sink;
	m_Channels = info->volume.channels;
	m_Appname  = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME);
	m_Volume   = pa_cvolume_avg(&info->volume);
	m_Kill     = false;
}

PAInterface::PAInterface(const char *context_name)
{
	m_Subscription_callback = nullptr;
	m_Mainloop              = pa_threaded_mainloop_new();

	assert(m_Mainloop);

	m_MainloopApi = pa_threaded_mainloop_get_api(m_Mainloop);
	m_Context     = pa_context_new(m_MainloopApi, context_name);
	assert(m_Context);

	pa_context_set_state_callback(m_Context, &PAInterface::cb_context_state, this);
	pa_threaded_mainloop_lock(m_Mainloop);

	assert(pa_threaded_mainloop_start(m_Mainloop) == 0);
	assert(pa_context_connect(m_Context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) == 0);

	for (;;)
	{
		pa_context_state_t state = pa_context_get_state(m_Context);
		assert(PA_CONTEXT_IS_GOOD(state));
		if (state == PA_CONTEXT_READY)
			break;
		pa_threaded_mainloop_wait(m_Mainloop);
	}

	pa_context_set_subscribe_callback(m_Context, &PAInterface::cb_subscription_event, this);
	pa_operation *subscrop = pa_context_subscribe(m_Context, PA_SUBSCRIPTION_MASK_ALL, &PAInterface::cb_success, this);

	while (pa_operation_get_state(subscrop) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(m_Mainloop);

	pa_threaded_mainloop_unlock(m_Mainloop);
	updateSinks();
	updateInputs();
}

PAInterface::~PAInterface()
{
	pa_context_disconnect(m_Context);
	pa_threaded_mainloop_stop(m_Mainloop);
	pa_threaded_mainloop_free(m_Mainloop);
}

void PAInterface::signal_mainloop(void *interface)
{
	pa_threaded_mainloop_signal(((PAInterface *)interface)->m_Mainloop, 0);
}

void PAInterface::cb_context_state(pa_context *context, void *interface)
{
	PAInterface::signal_mainloop((PAInterface *)interface);
}

void PAInterface::cb_success(pa_context *context, int success, void *interface)
{
	PAInterface::signal_mainloop((PAInterface *)interface);
}

void PAInterface::cb_subscription_event(pa_context *context, pa_subscription_event_type_t type, uint32_t idx, void *interface)
{
	pai_subscription_type_t paisubtype = 0x0U;
	if (pa_subscription_match_flags(PA_SUBSCRIPTION_MASK_SINK, type))
	{
		paisubtype = PAI_SUBSCRIPTION_MASK_SINK;
	}
	else if (pa_subscription_match_flags(PA_SUBSCRIPTION_MASK_SINK_INPUT, type))
	{
		paisubtype = PAI_SUBSCRIPTION_MASK_INPUT;
	}
	else
	{
		paisubtype = PAI_SUBSCRIPTION_MASK_OTHER;
	}

	std::thread updthread([=] {
		if (paisubtype == PAI_SUBSCRIPTION_MASK_INPUT)
		{
			PAInterface::_updateInputs((PAInterface *)interface);
		}
		else if (paisubtype == PAI_SUBSCRIPTION_MASK_SINK)
		{
			PAInterface::_updateSinks((PAInterface *)interface);
		}
		((PAInterface *)interface)->notifySubscription(paisubtype);
	});
	updthread.detach();
}

void PAInterface::cb_sink_input_info(pa_context *context, const pa_sink_input_info *info, int eol, void *interface)
{
	if (eol == 0)
	{
		std::map<uint32_t, InputInfo> &infomap = ((PAInterface *)interface)->m_Sinkinputinfos;
		infomap[info->index].update(info);
	}
	else
		PAInterface::signal_mainloop((PAInterface *)interface);
}

void PAInterface::cb_sink_info(pa_context *context, const pa_sink_info *info, int eol, void *interface)
{
	if (!eol)
	{
		std::map<uint32_t, SinkInfo> &infomap = ((PAInterface *)interface)->m_Sinkinfos;
		infomap[info->index] = SinkInfo(info);
	}
	else
		PAInterface::signal_mainloop((PAInterface *)interface);
}

void PAInterface::cb_read(pa_stream *stream, size_t nbytes, void *interface)
{
	const void *data;
	double      v;
	pa_stream_peek(stream, &data, &nbytes);

	if (!data)
	{
		if (nbytes)
			pa_stream_drop(stream);
		return;
	}
	assert(nbytes > 0);
	assert(nbytes % sizeof(float) == 0);

	v = ((const float *)data)[nbytes / sizeof(float) - 1];
	pa_stream_drop(stream);

	if (v < 0)
	{
		v = 0;
	}

	if (v > 1)
	{
		v = 1;
	}

	((PAInterface *)interface)->m_Sinkinputinfos[pa_stream_get_monitor_stream(stream)].m_Peak = v;
	((PAInterface *)interface)->notifySubscription(PAI_SUBSCRIPTION_MASK_PEAK);
}

void PAInterface::cb_suspend(pa_stream *stream, void *interface)
{
}

void PAInterface::_updateInputs(PAInterface *interface)
{
	mainloop_lockguard lg(interface->m_Mainloop);
	//dont clear, set kill flags to true and toggle in infocb then erase infos w/ flags afer operation
	//interface->m_Sinkinputinfos.clear();
	for (iter_inputinfo_t it = interface->m_Sinkinputinfos.begin(); it != interface->m_Sinkinputinfos.end(); it++)
		it->second.m_Kill = true;

	pa_operation *infooper = pa_context_get_sink_input_info_list(interface->m_Context, &PAInterface::cb_sink_input_info, interface);
	assert(infooper);

	while (pa_operation_get_state(infooper) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->m_Mainloop);

	pa_operation_unref(infooper);

	for (iter_inputinfo_t it = interface->m_Sinkinputinfos.begin(); it != interface->m_Sinkinputinfos.end();)
	{
		if (it->second.m_Kill)
		{
			it = interface->m_Sinkinputinfos.erase(it);
		}
		else
		{
			it++;
		}
	}
}

void PAInterface::_updateSinks(PAInterface *interface)
{
	mainloop_lockguard lg(interface->m_Mainloop);
	interface->m_Sinkinfos.clear();

	pa_operation *infooper = pa_context_get_sink_info_list(interface->m_Context, &PAInterface::cb_sink_info, interface);
	assert(infooper);

	while (pa_operation_get_state(infooper) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->m_Mainloop);

	pa_operation_unref(infooper);
}

void PAInterface::updateInputs()
{
	PAInterface::_updateInputs(this);
}

void PAInterface::updateSinks()
{
	PAInterface::_updateSinks(this);
}

int PAInterface::createMonitorStreamForSinkInput(iter_inputinfo_t &iiiter)
{
	mainloop_lockguard lg(m_Mainloop);

	if (!m_Sinkinfos.count(iiiter->second.m_Sink))
		return -1;
	uint32_t   monitorsrc = m_Sinkinfos[iiiter->second.m_Sink].m_MonitorSource;
	char       t[16];
	InputInfo &ii = iiiter->second;

	pa_buffer_attr    attr;
	pa_sample_spec    ss;
	pa_stream_flags_t flags;

	ss.channels = 1;
	ss.format   = PA_SAMPLE_FLOAT32;
	ss.rate     = 25;

	memset(&attr, 0, sizeof(attr));
	attr.fragsize  = sizeof(float);
	attr.maxlength = (uint32_t)-1;

	snprintf(t, sizeof(t), "%u", monitorsrc);

	if (ii.m_Monitor)
	{
		pa_stream_disconnect(ii.m_Monitor);
		pa_stream_unref(ii.m_Monitor);
		ii.m_Monitor = nullptr;
	}

	ii.m_Monitor = pa_stream_new(m_Context, "PeakDetect", &ss, NULL);
	assert(ii.m_Monitor);
	pa_stream_ref(ii.m_Monitor);

	pa_stream_set_monitor_stream(ii.m_Monitor, iiiter->first);

	pa_stream_set_read_callback(ii.m_Monitor, &PAInterface::cb_read, this);
	pa_stream_set_suspended_callback(ii.m_Monitor, &PAInterface::cb_suspend, this);

	flags = (pa_stream_flags_t)(PA_STREAM_DONT_MOVE | PA_STREAM_PEAK_DETECT | PA_STREAM_ADJUST_LATENCY);
	assert(pa_stream_connect_record(ii.m_Monitor, NULL, &attr, flags) >= 0);
	return 0;
}

void PAInterface::subscribe(pai_subscription_cb callback)
{
	m_Subscription_callback = callback;
	notifySubscription(0);
}

void PAInterface::notifySubscription(const pai_subscription_type_t type)
{
	if (m_Subscription_callback)
	{
		m_Subscription_callback(this, type);
	}
}

std::map<uint32_t, InputInfo> &PAInterface::getInputInfo()
{
	mainloop_lockguard lg(m_Mainloop);
	return m_Sinkinputinfos;
}

std::map<uint32_t, SinkInfo> &PAInterface::getSinkInfo()
{
	mainloop_lockguard lg(m_Mainloop);
	return m_Sinkinfos;
}

void PAInterface::addVolume(const uint32_t inputidx, const double pctDelta)
{
	mainloop_lockguard lg(m_Mainloop);

	int delta = round(pctDelta * PA_VOLUME_NORM);
	assert(getInputInfo().count(inputidx));

	pa_cvolume *volume = new pa_cvolume();
	pa_cvolume_init(volume);

	pa_volume_t vol = getInputInfo()[inputidx].m_Volume;

	if (delta > 0)
	{
		if (vol + delta > vol)
		{
			if (vol + delta > PA_VOLUME_NORM * 1.5)
			{
				vol = PA_VOLUME_NORM * 1.5;
			}
			else
			{
				vol += delta;
			}
		}
	}
	else
	{
		if (vol + delta < vol)
		{
			vol += delta;
		}
		else
		{
			vol = PA_VOLUME_MUTED;
		}
	}

	pa_cvolume_set(volume, getInputInfo()[inputidx].m_Channels, vol);

	pa_operation *op = pa_context_set_sink_input_volume(m_Context, inputidx, volume, &PAInterface::cb_success, this);
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(m_Mainloop);
	pa_operation_unref(op);
	delete volume;
}
