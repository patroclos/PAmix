#include <entry.hpp>

void SourceOutputEntry::update(const pa_source_output_info *info) {
	// general vars
	const char *name = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME);
	m_Name         = name != nullptr ? name : info->name;
	m_Index        = info->index;
	m_Mute         = info->mute;
	m_MonitorIndex = info->source;
	m_PAVolume     = info->volume;
	m_PAChannelMap = info->channel_map;
	m_Kill         = false;

	// stream vars
	m_Device = info->source;
}

void SourceOutputEntry::setVolume(const int channel, const pa_volume_t volume) {
	mainloop_lockguard lg(interface->getPAMainloop());

	pa_cvolume *cvol = &m_PAVolume;
	if (m_Lock)
		pa_cvolume_set(cvol, cvol->channels, volume);
	else
		cvol->values[channel] = volume;

	pa_operation *op = pa_context_set_source_output_volume(interface->getPAContext(), m_Index, cvol,
	                                                       &PAInterface::cb_success, interface);
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->getPAMainloop());
	pa_operation_unref(op);
}

void SourceOutputEntry::setMute(bool mute) {
	mainloop_lockguard lg(interface->getPAMainloop());
	pa_operation *op = pa_context_set_source_output_mute(interface->getPAContext(), m_Index, mute,
	                                                     &PAInterface::cb_success, interface);
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->getPAMainloop());
	pa_operation_unref(op);
}

void SourceOutputEntry::cycleSwitch(bool increment) {
	pamix_entry_iter_t source = interface->getSources().find(m_Device);

	if (increment)
		source++;
	else {
		if (source == interface->getSources().begin())
			source = std::next(source, interface->getSources().size() - 1);
		else
			source--;
	}
	if (source == interface->getSources().end())
		source            = interface->getSources().begin();

	pa_operation *op = pa_context_move_source_output_by_index(interface->getPAContext(), m_Index, source->second->m_Index,
	                                                          &PAInterface::cb_success, interface);
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->getPAMainloop());
	pa_operation_unref(op);
}

void SourceOutputEntry::move(uint32_t idx) {
	mainloop_lockguard lg(interface->getPAMainloop());
	pa_operation *op = pa_context_move_source_output_by_index(interface->getPAContext(), m_Index, idx,
	                                                          &PAInterface::cb_success, interface);

	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->getPAMainloop());
	pa_operation_unref(op);
}

void SourceOutputEntry::kill() {
	mainloop_lockguard lg(interface->getPAMainloop());
	pa_operation *op = pa_context_kill_source_output(interface->getPAContext(), m_Index, &PAInterface::cb_success,
	                                                 interface);

	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->getPAMainloop());
	pa_operation_unref(op);
}
