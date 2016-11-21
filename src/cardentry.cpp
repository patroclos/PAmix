#include <entry.hpp>

void CardEntry::update(const pa_card_info *info)
{
	m_Name  = pa_proplist_gets(info->proplist, PA_PROP_DEVICE_DESCRIPTION);
	m_Index = info->index;
	m_Lock  = false;
	m_Kill  = false;
	m_Meter = false;

	m_Profiles.clear();
	m_Profile = -1;
	for (unsigned i = 0; i < info->n_profiles; i++)
	{
		m_Profiles.push_back(info->profiles2[i]);
		if (info->profiles2[i] == info->active_profile2)
			m_Profile = i;
	}
}

void CardEntry::cycleSwitch(bool increment)
{
	mainloop_lockguard lg(interface->getPAMainloop());
	int delta = increment ? 1 : -1;

	if (!m_Profiles.size())
		return;

	m_Profile = (m_Profile + delta) % m_Profiles.size();

	pa_operation *op = pa_context_set_card_profile_by_index(interface->getPAContext(), m_Index, m_Profiles[m_Profile].name.c_str(), &PAInterface::cb_success, interface);
	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(interface->getPAMainloop());
	pa_operation_unref(op);
}
