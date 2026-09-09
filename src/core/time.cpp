#include "core/time.hpp"
#include <limits>
#include <numeric>

namespace nle {
namespace {
std::int64_t multiply(std::int64_t a, std::int64_t b) {
    if (a != 0 && b > std::numeric_limits<std::int64_t>::max() / a)
        throw DomainError("rational time overflow");
    return a * b;
}
} // namespace

RationalTime::RationalTime(std::int64_t value, std::int64_t rate) : value_(value), rate_(rate) {
    if (value < 0 || rate <= 0)
        throw DomainError("time must be nonnegative with a positive rate");
    const auto divisor = std::gcd(value, rate);
    value_ /= divisor;
    rate_ /= divisor;
}

std::strong_ordering RationalTime::operator<=>(const RationalTime &other) const {
    // Continued fractions compare exactly without overflowing cross products.
    auto a = value_;
    auto b = rate_;
    auto c = other.value_;
    auto d = other.rate_;
    bool reversed = false;
    for (;;) {
        const auto left = a / b;
        const auto right = c / d;
        if (left != right) {
            const bool less = (left < right) != reversed;
            return less ? std::strong_ordering::less : std::strong_ordering::greater;
        }
        const auto r1 = a % b;
        const auto r2 = c % d;
        if (r1 == 0 || r2 == 0) {
            if (r1 == r2)
                return std::strong_ordering::equal;
            const bool less = (r1 == 0) != reversed;
            return less ? std::strong_ordering::less : std::strong_ordering::greater;
        }
        a = b;
        b = r1;
        c = d;
        d = r2;
        reversed = !reversed;
    }
}

RationalTime RationalTime::operator+(const RationalTime &other) const {
    const auto common = std::gcd(rate_, other.rate_);
    const auto left = multiply(value_, other.rate_ / common);
    const auto right = multiply(other.value_, rate_ / common);
    if (right > std::numeric_limits<std::int64_t>::max() - left)
        throw DomainError("rational time overflow");
    return {left + right, multiply(rate_, other.rate_ / common)};
}

RationalTime RationalTime::operator-(const RationalTime &other) const {
    if (*this < other)
        throw DomainError("negative time result");
    const auto common = std::gcd(rate_, other.rate_);
    return {multiply(value_, other.rate_ / common) - multiply(other.value_, rate_ / common),
            multiply(rate_, other.rate_ / common)};
}
} // namespace nle
