#pragma once
#include <algorithm>
#include <cstdint>
namespace nle::desktop {
// QAudioSink's integral-microsecond clock can stop just short of the final sample.
inline std::int64_t device_sample_position(std::int64_t first, std::int64_t elapsed_us,
                                           std::int64_t submitted, std::int64_t total, bool idle) {
    if (idle && elapsed_us > 0 && submitted >= total)
        return total;
    return std::min(first + elapsed_us * 48000 / 1000000, total);
}
} // namespace nle::desktop
