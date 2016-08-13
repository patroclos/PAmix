#pragma once

#include <pulse/volume.h>
#include <ctgmath>

pa_cvolume *volume_pct_delta(pa_cvolume *vol, int channel, double pctDelta);
