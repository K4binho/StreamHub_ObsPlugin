#pragma once
#include <cmath>

// A wider release radius prevents jitter after the pointer reaches the center.
inline bool StreamHubCenterSnap(double distance, double threshold, bool &latched)
{
    const double radius = latched ? threshold * 2.0 : threshold;
    latched = threshold > 0 && std::abs(distance) <= radius;
    return latched;
}
