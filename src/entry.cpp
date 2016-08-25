#include <entry.hpp>

Entry::~Entry()
{
	if (m_Monitor && PA_STREAM_IS_GOOD(pa_stream_get_state(m_Monitor)))
	{
		pa_stream_disconnect(m_Monitor);
		pa_stream_unref(m_Monitor);
	}
}

void Entry::addVolume(PAInterface *interface, const int channel, const double deltaPct)
{
	volume_pct_delta(&m_PAVolume, channel, deltaPct);
	setVolume(interface, channel, m_Lock ? pa_cvolume_avg(&m_PAVolume) : m_PAVolume.values[channel]);
}
