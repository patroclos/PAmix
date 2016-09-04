#include <entry.hpp>

Entry::Entry(PAInterface *iface)
    : interface(iface)
{
}

Entry::~Entry()
{
	if (m_Monitor)
	{
		mainloop_lockguard mlg(interface->getPAMainloop());

		pa_stream_set_state_callback(m_Monitor, &PAInterface::cb_stream_state, nullptr);
		if (PA_STREAM_IS_GOOD(pa_stream_get_state(m_Monitor)))
		{
			pa_stream_disconnect(m_Monitor);
		}
		pa_stream_unref(m_Monitor);
		m_Monitor = nullptr;
	}
}

void Entry::addVolume(const int channel, const double deltaPct)
{
	volume_pct_delta(&m_PAVolume, channel, deltaPct);
	setVolume(channel, m_Lock ? pa_cvolume_avg(&m_PAVolume) : m_PAVolume.values[channel]);
}
