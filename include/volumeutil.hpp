#pragma once

#include <ctgmath>
#include <pulse/volume.h>

pa_cvolume *volume_pct_delta(pa_cvolume *vol, int channel, double pctDelta);
