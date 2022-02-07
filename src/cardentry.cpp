#include <entry.hpp>

void CardEntry::update(const pa_card_info *info) {
	m_Name = pa_proplist_gets(info->proplist, PA_PROP_DEVICE_DESCRIPTION);
	m_Index = info->index;
	m_Lock = false;
	m_Kill = false;
	m_Meter = false;
	m_PAVolume.channels = 0;

	m_Profile = -1;
	m_Profiles.clear();
	for (unsigned i = 0; i < info->n_profiles; i++) {
		m_Profiles.emplace_back(info->profiles2[i]);
		if (info->profiles2[i] == info->active_profile2)
			m_Profile = i;
	}
}

void CardEntry::cycleSwitch(bool increment) {
	mainloop_lockguard lg(interface->getPAMainloop());

	if (m_Profiles.empty())
		return;

	m_Profile = (m_Profile + (increment ? 1 : -1)) % (int) m_Profiles.size();
	if (m_Profile < 0)
		m_Profile += m_Profiles.size();

	pa_operation *op = pa_context_set_card_profile_by_index(interface->getPAContext(), m_Index,
	                                                        m_Profiles[m_Profile].name.c_str(), &PAInterface::cb_success,
	                                                        interface);
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->getPAMainloop());
	pa_operation_unref(op);
}
