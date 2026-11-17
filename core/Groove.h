#pragma once

#include "Note.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace roll
{
struct GrooveTemplate
{
    std::string name;
    std::string sourcePath;
    double gridBeats = 0.25;
    double timingAmount = 1.0;
    double velocityAmount = 1.0;
    std::vector<double> timingOffsets;
    std::vector<double> velocityMultipliers;

    bool isValid() const
    {
        if (name.empty() || ! std::isfinite (gridBeats) || gridBeats <= 0.0 || gridBeats > 4.0
            || ! std::isfinite (timingAmount) || timingAmount < 0.0 || timingAmount > 1.0
            || ! std::isfinite (velocityAmount) || velocityAmount < 0.0 || velocityAmount > 1.0
            || timingOffsets.empty() || timingOffsets.size() > 512
            || (! velocityMultipliers.empty() && velocityMultipliers.size() != timingOffsets.size()))
            return false;

        return std::all_of (timingOffsets.begin(), timingOffsets.end(),
                            [this] (double offset)
                            {
                                return std::isfinite (offset) && std::abs (offset) <= gridBeats;
                            })
            && std::all_of (velocityMultipliers.begin(), velocityMultipliers.end(),
                            [] (double multiplier)
                            {
                                return std::isfinite (multiplier) && multiplier >= 0.0 && multiplier <= 2.0;
                            });
    }
};

inline Note applyGroove (Note note, const GrooveTemplate& groove)
{
    if (! groove.isValid())
        return note;

    const auto slot = std::max<std::int64_t> (0,
        static_cast<std::int64_t> (std::llround (note.startBeats / groove.gridBeats)));
    const auto index = static_cast<std::size_t> (slot % static_cast<std::int64_t> (groove.timingOffsets.size()));
    note.startBeats = std::max (0.0, note.startBeats
        + groove.timingOffsets[index] * groove.timingAmount);

    if (! groove.velocityMultipliers.empty())
    {
        const double multiplier = 1.0 + (groove.velocityMultipliers[index] - 1.0) * groove.velocityAmount;
        note.velocity = std::clamp (static_cast<int> (std::lround (note.velocity * multiplier)), 1, 127);
    }

    return note;
}
} // namespace roll
