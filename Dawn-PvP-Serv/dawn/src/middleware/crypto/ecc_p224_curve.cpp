// own secp224r1 math: Windows has this curve in CNG but Wine does not.
// field values stay in the Montgomery domain between entry points

#include "ecc_p224_curve.h"

#include <algorithm>

namespace dawn::middleware::crypto::ecc::curve {
namespace {

constexpr unsigned kWordBits = 32;
constexpr std::size_t kWordBytes = 4;
constexpr unsigned kByteBits = 8;
constexpr std::uint32_t kAllBits = 0xFFFFFFFFU;

constexpr Field kPrime{
    0x00000001, 0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
constexpr Field kPrimeMinusTwo{
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
constexpr Field kOrder{
    0x5C5C2A3D, 0x13DD2945, 0xE0B8F03E, 0xFFFF16A2, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
constexpr Field kCoefficientB{
    0x2355FFB4, 0x270B3943, 0xD7BFD8BA, 0x5044B0B7, 0xF5413256, 0x0C04B3AB, 0xB4050A85};
constexpr Field kGeneratorX{
    0x115C1D21, 0x343280D6, 0x56C21122, 0x4A03C1D3, 0x321390B9, 0x6BB4BF7F, 0xB70E0CBD};
constexpr Field kGeneratorY{
    0x85007E34, 0x44D58199, 0x5A074764, 0xCD4375A0, 0x4C22DFE6, 0xB5F723FB, 0xBD376388};
constexpr Field kMontgomeryOne{
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000};
constexpr Field kMontgomeryR2{
    0x00000001, 0x00000000, 0x00000000, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000};
// -p^-1 mod 2^32; p ends in 1 so this is all bits
constexpr std::uint32_t kMontgomeryFactor = 0xFFFFFFFFU;

// Jacobian point: x = X/Z^2, y = Y/Z^3; Z of zero is infinity
struct Jacobian {
    Field x{};
    Field y{};
    Field z{};
};

[[nodiscard]] std::uint32_t
add_words(const Field& augend, const Field& addend, Field& sum) noexcept {
    std::uint64_t carry = 0;
    for (std::size_t index = 0; index < kWords; ++index) {
        const std::uint64_t total =
            static_cast<std::uint64_t>(augend[index]) + addend[index] + carry;
        sum[index] = static_cast<std::uint32_t>(total);
        carry = total >> kWordBits;
    }
    return static_cast<std::uint32_t>(carry);
}

[[nodiscard]] std::uint32_t
subtract_words(const Field& minuend, const Field& subtrahend, Field& difference) noexcept {
    std::uint64_t borrow = 0;
    for (std::size_t index = 0; index < kWords; ++index) {
        const std::uint64_t total =
            static_cast<std::uint64_t>(minuend[index]) - subtrahend[index] - borrow;
        difference[index] = static_cast<std::uint32_t>(total);
        borrow = (total >> kWordBits) & 1U;
    }
    return static_cast<std::uint32_t>(borrow);
}

[[nodiscard]] bool is_zero(const Field& value) noexcept {
    std::uint32_t bits = 0;
    for (const std::uint32_t word : value) {
        bits |= word;
    }
    return bits == 0;
}

[[nodiscard]] bool less_than(const Field& left, const Field& right) noexcept {
    Field ignored{};
    return subtract_words(left, right, ignored) != 0;
}

void select(bool take, const Field& first, const Field& second, Field& output) noexcept {
    const std::uint32_t mask = take ? kAllBits : 0U;
    for (std::size_t index = 0; index < kWords; ++index) {
        output[index] = (first[index] & mask) | (second[index] & ~mask);
    }
}

void montgomery_multiply(const Field& multiplicand,
                         const Field& multiplier,
                         Field& product) noexcept {
    std::array<std::uint32_t, kWords + 2> accumulator{};
    for (std::size_t step = 0; step < kWords; ++step) {
        std::uint64_t carry = 0;
        for (std::size_t index = 0; index < kWords; ++index) {
            const std::uint64_t sum =
                static_cast<std::uint64_t>(accumulator[index])
                + static_cast<std::uint64_t>(multiplicand[index]) * multiplier[step] + carry;
            accumulator[index] = static_cast<std::uint32_t>(sum);
            carry = sum >> kWordBits;
        }
        std::uint64_t top = static_cast<std::uint64_t>(accumulator[kWords]) + carry;
        accumulator[kWords] = static_cast<std::uint32_t>(top);
        accumulator[kWords + 1] = static_cast<std::uint32_t>(top >> kWordBits);

        const auto factor = static_cast<std::uint32_t>(static_cast<std::uint64_t>(accumulator[0])
                                                       * kMontgomeryFactor);
        carry = (static_cast<std::uint64_t>(accumulator[0])
                 + static_cast<std::uint64_t>(factor) * kPrime[0])
                >> kWordBits;
        for (std::size_t index = 1; index < kWords; ++index) {
            const std::uint64_t sum = static_cast<std::uint64_t>(accumulator[index])
                                      + static_cast<std::uint64_t>(factor) * kPrime[index] + carry;
            accumulator[index - 1] = static_cast<std::uint32_t>(sum);
            carry = sum >> kWordBits;
        }
        top = static_cast<std::uint64_t>(accumulator[kWords]) + carry;
        accumulator[kWords - 1] = static_cast<std::uint32_t>(top);
        accumulator[kWords] =
            accumulator[kWords + 1] + static_cast<std::uint32_t>(top >> kWordBits);
    }

    Field result{};
    std::copy_n(accumulator.begin(), kWords, result.begin());
    Field reduced{};
    const std::uint32_t borrow = subtract_words(result, kPrime, reduced);
    // result is below 2p, so one conditional subtraction always finishes it
    select(accumulator[kWords] != 0 || borrow == 0, reduced, result, product);
}

void field_add(const Field& augend, const Field& addend, Field& sum) noexcept {
    Field total{};
    const std::uint32_t carry = add_words(augend, addend, total);
    Field reduced{};
    const std::uint32_t borrow = subtract_words(total, kPrime, reduced);
    select(carry != 0 || borrow == 0, reduced, total, sum);
}

void field_subtract(const Field& minuend, const Field& subtrahend, Field& difference) noexcept {
    Field total{};
    const std::uint32_t borrow = subtract_words(minuend, subtrahend, total);
    Field wrapped{};
    (void)add_words(total, kPrime, wrapped);
    select(borrow != 0, wrapped, total, difference);
}

void to_montgomery(const Field& value, Field& output) noexcept {
    montgomery_multiply(value, kMontgomeryR2, output);
}

void from_montgomery(const Field& value, Field& output) noexcept {
    Field one{};
    one[0] = 1;
    montgomery_multiply(value, one, output);
}

void montgomery_inverse(const Field& value, Field& output) noexcept {
    Field result = kMontgomeryOne;
    for (std::size_t index = kWords; index-- > 0;) {
        for (unsigned bit = kWordBits; bit-- > 0;) {
            montgomery_multiply(result, result, result);
            if (((kPrimeMinusTwo[index] >> bit) & 1U) != 0) {
                montgomery_multiply(result, value, result);
            }
        }
    }
    output = result;
}

void jacobian_double(const Jacobian& point, Jacobian& output) noexcept {
    Field delta{};
    Field gamma{};
    Field beta{};
    Field alpha{};
    Field first{};
    Field second{};
    Field third{};
    montgomery_multiply(point.z, point.z, delta);
    montgomery_multiply(point.y, point.y, gamma);
    montgomery_multiply(point.x, gamma, beta);
    field_subtract(point.x, delta, first);
    field_add(point.x, delta, second);
    montgomery_multiply(first, second, third);
    field_add(third, third, alpha);
    field_add(alpha, third, alpha);

    Jacobian result{};
    montgomery_multiply(alpha, alpha, first);
    field_add(beta, beta, second);
    field_add(second, second, second);
    field_add(second, second, third);
    field_subtract(first, third, result.x);

    field_add(point.y, point.z, first);
    montgomery_multiply(first, first, first);
    field_subtract(first, gamma, first);
    field_subtract(first, delta, result.z);

    field_subtract(second, result.x, first);
    montgomery_multiply(alpha, first, first);
    montgomery_multiply(gamma, gamma, third);
    field_add(third, third, third);
    field_add(third, third, third);
    field_add(third, third, third);
    field_subtract(first, third, result.y);
    output = result;
}

void jacobian_add(const Jacobian& left, const Jacobian& right, Jacobian& output) noexcept {
    if (is_zero(left.z)) {
        output = right;
        return;
    }
    if (is_zero(right.z)) {
        output = left;
        return;
    }

    Field leftSquare{};
    Field rightSquare{};
    Field leftScaled{};
    Field rightScaled{};
    Field leftLine{};
    Field rightLine{};
    Field first{};
    Field second{};
    montgomery_multiply(left.z, left.z, leftSquare);
    montgomery_multiply(right.z, right.z, rightSquare);
    montgomery_multiply(left.x, rightSquare, leftScaled);
    montgomery_multiply(right.x, leftSquare, rightScaled);
    montgomery_multiply(right.z, rightSquare, first);
    montgomery_multiply(left.y, first, leftLine);
    montgomery_multiply(left.z, leftSquare, second);
    montgomery_multiply(right.y, second, rightLine);

    Field difference{};
    Field slope{};
    field_subtract(rightScaled, leftScaled, difference);
    field_subtract(rightLine, leftLine, slope);
    if (is_zero(difference)) {
        // equal points need the doubling formula; opposite points sum to infinity
        if (is_zero(slope)) {
            jacobian_double(left, output);
        } else {
            output = {};
        }
        return;
    }
    field_add(slope, slope, slope);

    Field square{};
    Field cube{};
    Field scaled{};
    field_add(difference, difference, square);
    montgomery_multiply(square, square, square);
    montgomery_multiply(difference, square, cube);
    montgomery_multiply(leftScaled, square, scaled);

    Jacobian result{};
    montgomery_multiply(slope, slope, first);
    field_subtract(first, cube, first);
    field_add(scaled, scaled, second);
    field_subtract(first, second, result.x);

    field_subtract(scaled, result.x, first);
    montgomery_multiply(slope, first, first);
    montgomery_multiply(leftLine, cube, second);
    field_add(second, second, second);
    field_subtract(first, second, result.y);

    field_add(left.z, right.z, first);
    montgomery_multiply(first, first, first);
    field_subtract(first, leftSquare, first);
    field_subtract(first, rightSquare, first);
    montgomery_multiply(first, difference, result.z);
    output = result;
}

void to_affine(const Jacobian& point, Point& output) noexcept {
    Field inverse{};
    Field square{};
    Field cube{};
    Field value{};
    montgomery_inverse(point.z, inverse);
    montgomery_multiply(inverse, inverse, square);
    montgomery_multiply(square, inverse, cube);
    montgomery_multiply(point.x, square, value);
    from_montgomery(value, output.x);
    montgomery_multiply(point.y, cube, value);
    from_montgomery(value, output.y);
}

} // namespace

Point generator() noexcept {
    return Point{kGeneratorX, kGeneratorY};
}

bool on_curve(const Point& point) noexcept {
    if (!less_than(point.x, kPrime) || !less_than(point.y, kPrime)) {
        return false;
    }
    Field x{};
    Field y{};
    to_montgomery(point.x, x);
    to_montgomery(point.y, y);

    Field left{};
    Field right{};
    Field term{};
    montgomery_multiply(y, y, left);
    montgomery_multiply(x, x, right);
    montgomery_multiply(right, x, right);
    field_add(x, x, term);
    field_add(term, x, term);
    field_subtract(right, term, right);
    to_montgomery(kCoefficientB, term);
    field_add(right, term, right);
    return left == right;
}

bool valid_scalar(const Field& scalar) noexcept {
    return !is_zero(scalar) && less_than(scalar, kOrder);
}

bool multiply(const Field& scalar, const Point& point, Point& output) noexcept {
    Jacobian base{};
    to_montgomery(point.x, base.x);
    to_montgomery(point.y, base.y);
    base.z = kMontgomeryOne;

    // run both branches of every bit so the scalar doesn't steer the work done
    Jacobian accumulator{};
    for (std::size_t index = kWords; index-- > 0;) {
        for (unsigned bit = kWordBits; bit-- > 0;) {
            Jacobian doubled{};
            jacobian_double(accumulator, doubled);
            Jacobian summed{};
            jacobian_add(doubled, base, summed);
            const bool take = ((scalar[index] >> bit) & 1U) != 0;
            select(take, summed.x, doubled.x, accumulator.x);
            select(take, summed.y, doubled.y, accumulator.y);
            select(take, summed.z, doubled.z, accumulator.z);
        }
    }
    if (is_zero(accumulator.z)) {
        return false;
    }
    to_affine(accumulator, output);
    return true;
}

void load(std::span<const std::byte> bytes, Field& output) noexcept {
    output = {};
    if (bytes.size() != kWords * kWordBytes) {
        return;
    }
    for (std::size_t index = 0; index < kWords; ++index) {
        const std::size_t offset = (kWords - 1 - index) * kWordBytes;
        std::uint32_t word = 0;
        for (std::size_t step = 0; step < kWordBytes; ++step) {
            word = (word << kByteBits) | std::to_integer<std::uint32_t>(bytes[offset + step]);
        }
        output[index] = word;
    }
}

void store(const Field& value, std::span<std::byte> bytes) noexcept {
    if (bytes.size() != kWords * kWordBytes) {
        return;
    }
    for (std::size_t index = 0; index < kWords; ++index) {
        const std::size_t offset = (kWords - 1 - index) * kWordBytes;
        for (std::size_t step = 0; step < kWordBytes; ++step) {
            const auto shift = static_cast<unsigned>((kWordBytes - 1 - step) * kByteBits);
            bytes[offset + step] = static_cast<std::byte>((value[index] >> shift) & 0xFFU);
        }
    }
}

} // namespace dawn::middleware::crypto::ecc::curve
