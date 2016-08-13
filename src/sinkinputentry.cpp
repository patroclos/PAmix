#include <entry.hpp>

void SinkInputEntry::update(const pa_sink_input_info *info)
{
	// general vars
	m_Name          = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME);
	m_Index         = info->index;
	m_Mute          = info->mute;
	m_PAVolume      = info->volume;
	m_PAChannelMap  = info->channel_map;
	m_Kill          = false;

	// stream vars
	m_Device = info->sink;
}

void SinkInputEntry::setVolume(PAInterface *interface, const int channel, const pa_volume_t volume)
{
	mainloop_lockguard lg(interface->getPAMainloop());

	pa_cvolume *cvol = &m_PAVolume;
	if (m_Lock)
		pa_cvolume_set(cvol, cvol->channels, volume);
	else
		cvol->values[channel] = volume;

	pa_operation *op = pa_context_set_sink_input_volume(interface->getPAContext(), m_Index, cvol, &PAInterface::cb_success, interface);
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->getPAMainloop());
	pa_operation_unref(op);
}

void SinkInputEntry::setMute(PAInterface *interface, bool mute)
{
	mainloop_lockguard lg(interface->getPAMainloop());
	pa_operation *     op = pa_context_set_sink_input_mute(interface->getPAContext(), m_Index, mute, &PAInterface::cb_success, interface);
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->getPAMainloop());
	pa_operation_unref(op);
}

void SinkInputEntry::cycleSwitch(PAInterface *interface, bool increment)
{
	iter_entry_t sink = interface->getSinks().find(m_Device);

	if (increment)
		sink++;
	else
	{
		if (sink == interface->getSinks().begin())
			sink = std::next(sink, interface->getSinks().size() - 1);
		else
			sink--;
	}

	if (sink == interface->getSinks().end())
		sink = interface->getSinks().begin();

	pa_operation *op = pa_context_move_sink_input_by_index(interface->getPAContext(), m_Index, sink->second->m_Index, &PAInterface::cb_success, interface);
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->getPAMainloop());
	pa_operation_unref(op);
}

void SinkInputEntry::move(PAInterface *interface, uint32_t idx)
{
	mainloop_lockguard lg(interface->getPAMainloop());
	pa_operation *     op = pa_context_move_sink_input_by_index(interface->getPAContext(), m_Index, idx, &PAInterface::cb_success, interface);

	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->getPAMainloop());
	pa_operation_unref(op);
}

void SinkInputEntry::kill(PAInterface *interface)
{
	mainloop_lockguard lg(interface->getPAMainloop());
	pa_operation *     op = pa_context_kill_sink_input(interface->getPAContext(), m_Index, &PAInterface::cb_success, interface);

	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->getPAMainloop());
	pa_operation_unref(op);
}
