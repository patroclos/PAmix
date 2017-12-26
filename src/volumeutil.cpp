#include <volumeutil.hpp>

pa_cvolume *volume_pct_delta(pa_cvolume *volume, int channel, double pctDelta) {
	int delta = round(pctDelta * PA_VOLUME_NORM);

	pa_volume_t vol = channel == -1 ? pa_cvolume_avg(volume) : volume->values[channel];
	if (delta > 0) {
		if (vol + delta > vol) {
			if (vol + delta > PA_VOLUME_NORM * 1.5)
				vol = PA_VOLUME_NORM * 1.5;
			else
				vol += delta;
		}
	} else {
		if (vol + delta < vol)
			vol += delta;
		else
			vol = PA_VOLUME_MUTED;
	}

	if (channel == -1)
		pa_cvolume_set(volume, volume->channels, vol);
	else
		volume->values[channel] = vol;
	return volume;
}
