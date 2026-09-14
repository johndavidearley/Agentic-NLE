#pragma once
#include "core/time.hpp"
namespace nle {
// Signed source timestamps are separate from nonnegative edit positions/durations.
class SourceTime {
  public:
    SourceTime(std::int64_t value = 0, std::int64_t rate = 1);
    static SourceTime from_ticks(std::int64_t ticks, RationalTime time_base);
    bool operator==(const SourceTime &) const = default;
    std::strong_ordering operator<=>(const SourceTime &other) const;
    RationalTime since(const SourceTime &origin) const;
    RationalTime magnitude() const { return magnitude_; }
    bool negative() const { return negative_; }

  private:
    RationalTime magnitude_;
    bool negative_ = false;
};
} // namespace nle
