#include <painterface.hpp>

mainloop_lockguard::mainloop_lockguard(pa_threaded_mainloop *m)
		: m(m) {
	pa_threaded_mainloop_lock(m);
}

mainloop_lockguard::~mainloop_lockguard() {
	pa_threaded_mainloop_unlock(m);
}

PAInterface::PAInterface(const char *context_name)
		: m_Autospawn(false), m_ContextName(context_name), m_Mainloop(nullptr), m_MainloopApi(nullptr), m_Context() {
}

PAInterface::~PAInterface() {
	cleanupPulseObjects();
}

void PAInterface::signal_mainloop(void *interface) {
	pa_threaded_mainloop_signal(((PAInterface *) interface)->m_Mainloop, 0);
}

void PAInterface::cb_context_state(pa_context *context, void *userdata) {
	auto interface = static_cast<PAInterface *>(userdata);
	if (PA_CONTEXT_IS_GOOD(pa_context_get_state(context)))
		PAInterface::signal_mainloop(interface);
	else {
		interface->m_Sinks.clear();
		interface->m_SinkInputs.clear();
		interface->m_Sources.clear();
		interface->m_SourceOutputs.clear();
		interface->m_Cards.clear();
	}
	interface->notifySubscription(PAI_SUBSCRIPTION_MASK_CONNECTION_STATUS);
}

void PAInterface::cb_context_drain_complete(pa_context *context, void *) {
	pa_context_disconnect(context);
}

void PAInterface::cb_success(pa_context *, int, void *interface) {
	PAInterface::signal_mainloop((PAInterface *) interface);
}

void PAInterface::_updatethread(pai_subscription_type_t paisubtype, pa_subscription_event_type_t type,
                                PAInterface *interface) {
	if (paisubtype == PAI_SUBSCRIPTION_MASK_INFO) {
		if (pa_subscription_match_flags(PA_SUBSCRIPTION_MASK_SINK, type))
			PAInterface::_updateSinks(interface);
		else if (pa_subscription_match_flags(PA_SUBSCRIPTION_MASK_SOURCE, type))
			PAInterface::_updateSources(interface);
		else if (pa_subscription_match_flags(PA_SUBSCRIPTION_MASK_SINK_INPUT, type))
			PAInterface::_updateInputs(interface);
		else if (pa_subscription_match_flags(PA_SUBSCRIPTION_MASK_SOURCE_OUTPUT, type))
			PAInterface::_updateOutputs(interface);
		else if (pa_subscription_match_flags(PA_SUBSCRIPTION_MASK_CARD, type))
			PAInterface::_updateCards(interface);
	}
	interface->notifySubscription(paisubtype);
}

void PAInterface::cb_subscription_event(pa_context *, pa_subscription_event_type_t type, uint32_t, void *interface) {
	pai_subscription_type_t paisubtype;
	if (pa_subscription_match_flags(
			PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE | PA_SUBSCRIPTION_MASK_SINK_INPUT |
			PA_SUBSCRIPTION_MASK_SOURCE_OUTPUT | PA_SUBSCRIPTION_MASK_CARD, type)) {
		paisubtype = PAI_SUBSCRIPTION_MASK_INFO;
	} else {
		paisubtype = PAI_SUBSCRIPTION_MASK_OTHER;
	}

	std::thread updthread(_updatethread, paisubtype, type, (PAInterface *) interface);
	updthread.detach();
}

void PAInterface::cb_sink_info(pa_context *, const pa_sink_info *info, int eol, void *interface) {
	if (!eol) {
		std::map<uint32_t, std::unique_ptr<Entry>> &map = ((PAInterface *) interface)->m_Sinks;

		std::lock_guard<std::mutex> lg(((PAInterface *) interface)->m_ModifyMutex);
		if (!map.count(info->index) || !map[info->index])
			map[info->index] = std::unique_ptr<Entry>(new SinkEntry((PAInterface *) interface));
		map[info->index]->update(info);
	} else
		PAInterface::signal_mainloop((PAInterface *) interface);
}

void PAInterface::cb_source_info(pa_context *, const pa_source_info *info, int eol, void *interface) {
	if (!eol) {
		std::map<uint32_t, std::unique_ptr<Entry>> &map = ((PAInterface *) interface)->m_Sources;
		std::lock_guard<std::mutex> lg(((PAInterface *) interface)->m_ModifyMutex);
		if (!map.count(info->index) || !map[info->index])
			map[info->index] = std::unique_ptr<Entry>(new SourceEntry((PAInterface *) interface));
		map[info->index]->update(info);
	} else
		PAInterface::signal_mainloop((PAInterface *) interface);
}

void PAInterface::cb_sink_input_info(pa_context *, const pa_sink_input_info *info, int eol, void *interface) {
	if (!eol) {
		std::map<uint32_t, std::unique_ptr<Entry>> &map = ((PAInterface *) interface)->m_SinkInputs;

		std::lock_guard<std::mutex> lg(((PAInterface *) interface)->m_ModifyMutex);
		if (!map.count(info->index) || !map[info->index])
			map[info->index] = std::unique_ptr<Entry>(new SinkInputEntry((PAInterface *) interface));
		map[info->index]->update(info);
	} else
		PAInterface::signal_mainloop((PAInterface *) interface);
}

void
PAInterface::cb_source_output_info(pa_context *, const pa_source_output_info *info, int eol, void *interface) {
	if (!eol) {
		const char *appid = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_ID);
		if (appid) {
			if (strcmp(appid, "pamix") == 0) {
				const pa_cvolume *cv = &info->volume;
				if (cv->channels == 1 && cv->values[0] != PA_VOLUME_NORM) {
					auto *iface = (PAInterface *) interface;
					pa_cvolume cvol{};
					pa_cvolume_set(&cvol, 1, PA_VOLUME_NORM);
					pa_context_set_source_output_volume(iface->getPAContext(), info->index, &cvol, nullptr, nullptr);
				}
				return;
			}
			if (strcmp(appid, "org.PulseAudio.pavucontrol") != 0)
				return;
		}
		std::map<uint32_t, std::unique_ptr<Entry>> &map = ((PAInterface *) interface)->m_SourceOutputs;
		std::lock_guard<std::mutex> lg(((PAInterface *) interface)->m_ModifyMutex);
		if (!map.count(info->index) || !map[info->index])
			map[info->index] = std::unique_ptr<Entry>(new SourceOutputEntry((PAInterface *) interface));
		map[info->index]->update(info);
	} else
		PAInterface::signal_mainloop((PAInterface *) interface);
}

void PAInterface::cb_card_info(pa_context *, const pa_card_info *info, int eol, void *interface) {
	if (!eol) {
		std::map<uint32_t, std::unique_ptr<Entry>> &map = ((PAInterface *) interface)->m_Cards;

		std::lock_guard<std::mutex> lg(((PAInterface *) interface)->m_ModifyMutex);
		if (!map.count(info->index) || !map[info->index])
			map[info->index] = std::unique_ptr<Entry>(new CardEntry((PAInterface *) interface));
		map[info->index]->update(info);
	} else
		PAInterface::signal_mainloop((PAInterface *) interface);
}

void PAInterface::cb_read(pa_stream *stream, size_t nbytes, void *iepair) {
	auto *pair = (std::pair<PAInterface *, Entry *> *) (iepair);

	if (!pair->second || !pair->first->m_Context)
		return;

	const void *data;
	float v;
	if (pa_stream_peek(stream, &data, &nbytes) < 0)
		return;

	if (!data) {
		if (nbytes)
			pa_stream_drop(stream);
		return;
	}
	assert(nbytes > 0);
	assert(nbytes % sizeof(float) == 0);

	v = ((const float *) data)[nbytes / sizeof(float) - 1];
	pa_stream_drop(stream);

	if (v < 0)
		v = 0;
	else if (v > 1)
		v = 1;

	pair->second->m_Peak = v;
	pair->first->notifySubscription(PAI_SUBSCRIPTION_MASK_PEAK);
}

void PAInterface::cb_stream_state(pa_stream *stream, void *entry) {
	if (!entry)
		return;
	pa_stream_state_t state = pa_stream_get_state(stream);
	if (state == PA_STREAM_TERMINATED || state == PA_STREAM_FAILED) {
		((Entry *) entry)->m_Monitor = nullptr;
	}
}

void __updateEntries(PAInterface *interface, std::map<uint32_t, std::unique_ptr<Entry>> &map, entry_type entrytype) {
	mainloop_lockguard lg(interface->getPAMainloop());

	{
		std::lock_guard<std::mutex> mlg(interface->m_ModifyMutex);
		for (auto it = map.begin(); it != map.end();)
			if (it->second)
				it++->second->m_Kill = true;
			else
				it = map.erase(it);
	}

	pa_operation *infooper = nullptr;
	switch (entrytype) {
		case ENTRY_SINK:
			infooper = pa_context_get_sink_info_list(interface->getPAContext(), &PAInterface::cb_sink_info, interface);
			break;
		case ENTRY_SOURCE:
			infooper = pa_context_get_source_info_list(interface->getPAContext(), &PAInterface::cb_source_info, interface);
			break;
		case ENTRY_SINKINPUT:
			infooper = pa_context_get_sink_input_info_list(interface->getPAContext(), &PAInterface::cb_sink_input_info,
			                                               interface);
			break;
		case ENTRY_SOURCEOUTPUT:
			infooper = pa_context_get_source_output_info_list(interface->getPAContext(), &PAInterface::cb_source_output_info,
			                                                  interface);
			break;
		case ENTRY_CARDS:
			infooper = pa_context_get_card_info_list(interface->getPAContext(), &PAInterface::cb_card_info, interface);
			break;
		default:
			return;
	}
	assert(infooper);

	while (pa_operation_get_state(infooper) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->getPAMainloop());
	pa_operation_unref(infooper);

	std::lock_guard<std::mutex> modlg(interface->m_ModifyMutex);
	for (auto it = map.begin(); it != map.end();) {
		if (!it->second) {
			it = map.erase(it);
			continue;
		}
		if (it->second->m_Kill) {
			for (auto pairIterator = interface->m_IEPairs.begin();
			     pairIterator != interface->m_IEPairs.end();) {
				if ((*pairIterator)->second == it->second.get()) {
					(*pairIterator)->second = nullptr;
					break;
				}
				pairIterator++;
			}
			it = map.erase(it);
		} else {
			if (!it->second->m_Monitor)
				interface->createMonitorStreamForEntry(it->second.get(), entrytype);
			it++;
		}
	}
}

void PAInterface::_updateSinks(PAInterface *interface) {
	__updateEntries(interface, interface->getSinks(), ENTRY_SINK);
}

void PAInterface::_updateSources(PAInterface *interface) {
	__updateEntries(interface, interface->getSources(), ENTRY_SOURCE);
}

void PAInterface::_updateInputs(PAInterface *interface) {
	__updateEntries(interface, interface->getSinkInputs(), ENTRY_SINKINPUT);
}

void PAInterface::_updateOutputs(PAInterface *interface) {
	__updateEntries(interface, interface->getSourceOutputs(), ENTRY_SOURCEOUTPUT);
}

void PAInterface::_updateCards(PAInterface *interface) {
	__updateEntries(interface, interface->getCards(), ENTRY_CARDS);
}

bool PAInterface::tryCreateConnection() {
	m_Mainloop = pa_threaded_mainloop_new();
	assert(m_Mainloop);

	m_MainloopApi = pa_threaded_mainloop_get_api(m_Mainloop);

	pa_proplist *plist = pa_proplist_new();
	pa_proplist_sets(plist, PA_PROP_APPLICATION_ID, m_ContextName);
	pa_proplist_sets(plist, PA_PROP_APPLICATION_NAME, m_ContextName);
	m_Context = pa_context_new_with_proplist(m_MainloopApi, nullptr, plist);
	pa_proplist_free(plist);

	assert(m_Context);
	pa_context_set_state_callback(m_Context, &PAInterface::cb_context_state, this);

	pa_threaded_mainloop_lock(m_Mainloop);

	if (pa_threaded_mainloop_start(m_Mainloop)) {
		pa_threaded_mainloop_unlock(m_Mainloop);
		return false;
	}

	pa_context_flags flags = m_Autospawn ? PA_CONTEXT_NOFLAGS : PA_CONTEXT_NOAUTOSPAWN;
	if (pa_context_connect(m_Context, nullptr, flags, nullptr)) {
		pa_threaded_mainloop_unlock(m_Mainloop);
		return false;
	}

	for (;;) {
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
	pa_operation_unref(subscrop);

	pa_threaded_mainloop_unlock(m_Mainloop);

	updateSinks();
	updateSources();
	updateInputs();
	updateOutputs();
	updateCards();
	return true;
}

bool PAInterface::connect() {
	bool success = tryCreateConnection();
	if (!success)
		cleanupPulseObjects();
	return success;
}

void PAInterface::cleanupPulseObjects() {
	m_Sinks.clear();
	m_Sources.clear();
	m_SinkInputs.clear();
	m_SourceOutputs.clear();
	m_Cards.clear();
	if (m_Context) {
		pa_operation *o;
		if (!(o = pa_context_drain(m_Context, &PAInterface::cb_context_drain_complete, nullptr)))
			pa_context_disconnect(m_Context);
		else {
			pa_operation_unref(o);
		}
		m_Context = nullptr;
	}

	if (m_Mainloop) {
		//pa_threaded_mainloop_unlock(m_Mainloop);
		//pa_threaded_mainloop_stop(m_Mainloop);
		pa_threaded_mainloop_free(m_Mainloop);
		m_Mainloop = nullptr;
	}

	m_MainloopApi = nullptr;
}

bool PAInterface::isConnected() {
	return m_Context ? pa_context_get_state(m_Context) == PA_CONTEXT_READY : false;
}

pa_stream *_createMonitor(PAInterface *interface, uint32_t source, Entry *entry, uint32_t stream) {
	pa_stream *s;
	char t[16];
	pa_buffer_attr attr{};
	pa_sample_spec ss{};
	pa_stream_flags_t flags;

	ss.channels = 1;
	ss.format = PA_SAMPLE_FLOAT32;
	ss.rate = 25;

	memset(&attr, 0, sizeof(attr));
	attr.fragsize = sizeof(float);
	attr.maxlength = (uint32_t) -1;

	snprintf(t, sizeof(t), "%u", source);

	s = pa_stream_new(interface->getPAContext(), "PeakDetect", &ss, nullptr);
	if (!s)
		return nullptr;
	if (stream != (uint32_t) -1)
		pa_stream_set_monitor_stream(s, stream);

	auto *pair = new std::pair<PAInterface *, Entry *>();
	pair->first = interface;
	pair->second = entry;
	interface->m_IEPairs.emplace_back(std::unique_ptr<std::pair<PAInterface *, Entry *>>(pair));

	pa_stream_set_read_callback(s, &PAInterface::cb_read, pair);
	pa_stream_set_state_callback(s, &PAInterface::cb_stream_state, entry);

	flags = (pa_stream_flags_t) (PA_STREAM_DONT_MOVE | PA_STREAM_PEAK_DETECT | PA_STREAM_ADJUST_LATENCY);
	if (pa_stream_connect_record(s, (stream != (uint32_t) -1) ? nullptr : t, &attr, flags) < 0) {
		pa_stream_unref(s);
		return nullptr;
	}

	return s;
}

void PAInterface::createMonitorStreamForEntry(Entry *entry, int type) {
	if (entry->m_Monitor) {
		pa_stream_disconnect(entry->m_Monitor);
		entry->m_Monitor = nullptr;
	}

	if (type == ENTRY_SINKINPUT) {
		uint32_t dev = ((SinkInputEntry *) entry)->m_Device;
		if (m_Sinks.count(dev) && m_Sinks[dev])
			entry->m_Monitor = _createMonitor(this, m_Sinks[dev]->m_Index, entry, entry->m_Index);
	} else if (type != ENTRY_CARDS) {
		entry->m_Monitor = _createMonitor(this, entry->m_MonitorIndex, entry, (uint32_t) -1);
	}
}

void PAInterface::subscribe(pai_subscription_cb callback) {
	m_Subscription_callback = callback;
}

void PAInterface::notifySubscription(const pai_subscription_type_t type) {
	if (m_Subscription_callback) {
		m_Subscription_callback(this, type);
	}
}

