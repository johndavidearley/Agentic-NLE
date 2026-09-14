#include "core/source_time.hpp"
#include <limits>
#include <numeric>
namespace nle {
SourceTime::SourceTime(std::int64_t value, std::int64_t rate) {
    if (value == std::numeric_limits<std::int64_t>::min())
        throw DomainError("source timestamp magnitude overflow");
    magnitude_ = {value < 0 ? -value : value, rate};
    negative_ = value < 0;
}
SourceTime SourceTime::from_ticks(std::int64_t ticks, RationalTime base) {
    SourceTime value(ticks);
    const auto common = std::gcd(value.magnitude_.value(), base.rate());
    const auto reduced = value.magnitude_.value() / common;
    if (base.value() == 0 || reduced > std::numeric_limits<std::int64_t>::max() / base.value())
        throw DomainError("source timestamp scale overflow");
    const auto numerator = reduced * base.value();
    return {value.negative_ ? -numerator : numerator, base.rate() / common};
}
std::strong_ordering SourceTime::operator<=>(const SourceTime &other) const {
    if (negative_ != other.negative_)
        return negative_ ? std::strong_ordering::less : std::strong_ordering::greater;
    return negative_ ? other.magnitude_ <=> magnitude_ : magnitude_ <=> other.magnitude_;
}
RationalTime SourceTime::since(const SourceTime &origin) const {
    if (*this < origin)
        throw DomainError("timestamp precedes source origin");
    if (negative_ != origin.negative_)
        return magnitude_ + origin.magnitude_;
    return negative_ ? origin.magnitude_ - magnitude_ : magnitude_ - origin.magnitude_;
}
} // namespace nle
