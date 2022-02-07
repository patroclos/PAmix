#include <entry.hpp>

void SourceEntry::update(const pa_source_info *info) {
	m_Name = info->description;
	m_Index = info->index;
	m_Mute = info->mute != 0;
	m_MonitorIndex = m_Index;
	m_PAVolume = info->volume;
	m_PAChannelMap = info->channel_map;
	m_Kill = false;

	m_Port = -1;
	m_Ports.clear();
	for (unsigned i = 0; i < info->n_ports; i++) {
		auto port = info->ports[i];
		m_Ports.emplace_back(port->name, port->description);
		if (info->active_port == port)
			m_Port = i;
	}
	m_State = info->state;
}

void SourceEntry::setVolume(const int channel, const pa_volume_t volume) {
	mainloop_lockguard lg(interface->getPAMainloop());

	pa_cvolume *cvol = &m_PAVolume;
	if (m_Lock)
		pa_cvolume_set(cvol, cvol->channels, volume);
	else
		cvol->values[channel] = volume;

	pa_operation *op = pa_context_set_source_volume_by_index(interface->getPAContext(), m_Index, cvol,
	                                                         &PAInterface::cb_success, interface);
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->getPAMainloop());
	pa_operation_unref(op);
}

void SourceEntry::setMute(bool mute) {
	mainloop_lockguard lg(interface->getPAMainloop());

	pa_operation *op = pa_context_set_source_mute_by_index(interface->getPAContext(), m_Index, mute,
	                                                       &PAInterface::cb_success, interface);
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->getPAMainloop());
	pa_operation_unref(op);
}

void SourceEntry::cycleSwitch(bool increment) {
	int delta = increment ? 1 : -1;
	if (m_Ports.empty())
		return;
	m_Port = (m_Port + delta) % (unsigned) m_Ports.size();

	pa_operation *op = pa_context_set_source_port_by_index(interface->getPAContext(), m_Index,
	                                                       m_Ports[m_Port].name.c_str(),
	                                                       &PAInterface::cb_success, interface);
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->getPAMainloop());
	pa_operation_unref(op);
}

void SourceEntry::setPort(const char *port) {
	mainloop_lockguard lg(interface->getPAMainloop());

	pa_operation *op = pa_context_set_source_port_by_index(interface->getPAContext(), m_Index, port,
	                                                       &PAInterface::cb_success, interface);

	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->getPAMainloop());
	pa_operation_unref(op);
}
