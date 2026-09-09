#pragma once
#include <compare>
#include <cstdint>
#include <stdexcept>

namespace nle {
class DomainError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// A nonnegative exact duration/position in seconds, reduced to value/rate.
// Signed offsets and rounding policies are deliberately outside milestone 1.
class RationalTime {
  public:
    RationalTime(std::int64_t value = 0, std::int64_t rate = 1);
    [[nodiscard]] std::int64_t value() const { return value_; }
    [[nodiscard]] std::int64_t rate() const { return rate_; }
    bool operator==(const RationalTime &) const = default;
    std::strong_ordering operator<=>(const RationalTime &other) const;
    RationalTime operator+(const RationalTime &other) const;
    RationalTime operator-(const RationalTime &other) const;

  private:
    std::int64_t value_;
    std::int64_t rate_;
};

struct TimeRange {
    RationalTime start;
    RationalTime duration;
    [[nodiscard]] RationalTime end() const { return start + duration; }
    bool operator==(const TimeRange &) const = default;
};
} // namespace nle
