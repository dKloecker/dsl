#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "dslu_fixed_decimal.h"

namespace dsl::test {

using std::int8_t;
using std::int16_t;
using std::int32_t;
using std::int64_t;

template <typename F>
static std::string Str(const F& f) {
    std::ostringstream os;
	os << f;
    return os.str();
}

template <typename T, std::size_t P>
static void ExpectIntRoundTrips() {
    using F = fixed_decimal_t<T, P>;
    const T max_i = F::max_integer();
    const T min_i = F::min_integer();

    std::vector<T> checkpoints = {
        T{0}, T{1}, T{-1},
        static_cast<T>(max_i / 2),
        static_cast<T>(min_i / 2),
        max_i,
        min_i,
    };

    for (const T v : checkpoints) {
        const F fixed{v};
        EXPECT_EQ(fixed.template to<T>(), v)
            << "round trip failed for T=" << sizeof(T) * 8
            << "bit P=" << P << " value=" << static_cast<long long>(v);
    }
}

TEST(RoundTrip, Int8)  { ExpectIntRoundTrips<int8_t, 1>(); }   // int8 max=127 -> P=1 only
TEST(RoundTrip, Int16) { ExpectIntRoundTrips<int16_t, 2>(); }
TEST(RoundTrip, Int32) { ExpectIntRoundTrips<int32_t, 2>(); }
TEST(RoundTrip, Int64) { ExpectIntRoundTrips<int64_t, 2>(); }

TEST(RoundTrip, Precision0) { ExpectIntRoundTrips<int64_t, 0>(); }
TEST(RoundTrip, Precision1) { ExpectIntRoundTrips<int64_t, 1>(); }
TEST(RoundTrip, Precision2) { ExpectIntRoundTrips<int64_t, 2>(); }
TEST(RoundTrip, Precision4) { ExpectIntRoundTrips<int64_t, 4>(); }
TEST(RoundTrip, Precision9) { ExpectIntRoundTrips<int64_t, 9>(); }

TEST(Construct, DefaultIsZero) {
    fixed_decimal_t<int64_t, 2> z;
    EXPECT_EQ(z.raw(), 0);
    EXPECT_EQ(z.to<int>(), 0);
}

TEST(Construct, FromIntegerScales) {
    fixed_decimal_t<int64_t, 2> a{5};
    EXPECT_EQ(a.raw(), 500);           // 5 * 10^2
}

TEST(Construct, FromFloatRoundsHalfAwayFromZero) {
    fixed_decimal_t<int64_t, 2> up{2.346};    // 234.6 -> 235
    EXPECT_EQ(up.raw(), 235);
    fixed_decimal_t<int64_t, 2> down{2.344};  // 234.4 -> 234
    EXPECT_EQ(down.raw(), 234);
    fixed_decimal_t<int64_t, 2> negUp{-2.346};
    EXPECT_EQ(negUp.raw(), -235);
    fixed_decimal_t<int64_t, 2> negDown{-2.344};
    EXPECT_EQ(negDown.raw(), -234);
}

TEST(Extract, ToIntTruncatesFraction) {
    fixed_decimal_t<int64_t, 2> a{2.99};
    EXPECT_EQ(a.to<int>(), 2);
}

TEST(Extract, ToDoubleReconstructs) {
    fixed_decimal_t<int64_t, 2> a{292.09};
    EXPECT_DOUBLE_EQ(a.to<double>(), 292.09);
}


class Arith : public ::testing::Test {
protected:
    using F = fixed_decimal_t<int64_t, 2>;
    F a{292.09};
    F b{7.91};
};

TEST_F(Arith, Add)      { EXPECT_EQ((a + b).raw(), 30000); }   // 300.00
TEST_F(Arith, Subtract) { EXPECT_EQ((a - b).raw(), 28418); }   // 284.18
TEST_F(Arith, Multiply) { EXPECT_EQ((a * b).raw(), 231043); }  // 2310.4319 -> 2310.43
TEST_F(Arith, Divide)   { EXPECT_EQ((a / b).raw(), 3692); }    // 36.92...
TEST_F(Arith, Negate)   { EXPECT_EQ((-a).raw(), -29209); }

TEST_F(Arith, CompoundAdd) {
    F c = a; c += b;
    EXPECT_EQ(c.raw(), 30000);
}
TEST_F(Arith, CompoundSub) {
    F c = a; c -= b;
    EXPECT_EQ(c.raw(), 28418);
}
TEST_F(Arith, CompoundMul) {
    F c = a; c *= b;
    EXPECT_EQ(c.raw(), 231043);
}
TEST_F(Arith, CompoundDiv) {
    F c = a; c /= b;
    EXPECT_EQ(c.raw(), 3692);
}

TEST(Arith2, MultiplyIdentity) {
    fixed_decimal_t<int64_t, 2> x{12.34};
    fixed_decimal_t<int64_t, 2> one{1};
    EXPECT_EQ((x * one).raw(), x.raw());
}

TEST(Arith2, DivideByOne) {
    fixed_decimal_t<int64_t, 4> x{3.1416};
    fixed_decimal_t<int64_t, 4> one{1};
    EXPECT_EQ((x / one).raw(), x.raw());
}

TEST(IncDec, PreIncrementWholeUnit) {
    fixed_decimal_t<int64_t, 2> c{10.0};
    EXPECT_EQ((++c).raw(), 1100);   // 11.00
    EXPECT_EQ(c.raw(), 1100);
}

TEST(IncDec, PostIncrementReturnsOld) {
    fixed_decimal_t<int64_t, 2> c{10.0};
    fixed_decimal_t<int64_t, 2> old = c++;
    EXPECT_EQ(old.raw(), 1000);     // 10.00
    EXPECT_EQ(c.raw(), 1100);       // 11.00
}

TEST(IncDec, PreDecrement) {
    fixed_decimal_t<int64_t, 2> c{10.0};
    EXPECT_EQ((--c).raw(), 900);    // 9.00
}

TEST(IncDec, PostDecrementReturnsOld) {
    fixed_decimal_t<int64_t, 2> c{10.0};
    auto old = c--;
    EXPECT_EQ(old.raw(), 1000);
    EXPECT_EQ(c.raw(), 900);
}

// ============================================================================
// Comparison
// ============================================================================

TEST(Compare, Ordering) {
    fixed_decimal_t<int64_t, 2> a{1.50}, b{2.50}, c{1.50};
    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b > a);
    EXPECT_TRUE(a <= c);
    EXPECT_TRUE(a >= c);
    EXPECT_TRUE(a == c);
    EXPECT_TRUE(a != b);
}

TEST(Compare, NegativeOrdering) {
    fixed_decimal_t<int64_t, 2> neg{-5.00}, zero{0}, pos{5.00};
    EXPECT_TRUE(neg < zero);
    EXPECT_TRUE(zero < pos);
}

// ============================================================================
// abs
// ============================================================================

TEST(Abs, NegativeBecomesPositive) {
    fixed_decimal_t<int64_t, 2> n{-4.30};
    EXPECT_EQ(n.abs().raw(), 430);
}
TEST(Abs, PositiveUnchanged) {
    fixed_decimal_t<int64_t, 2> p{4.30};
    EXPECT_EQ(p.abs().raw(), 430);
}

// ============================================================================
// Cross-precision conversion
// ============================================================================

TEST(Convert, WideningPreservesValue) {
    fixed_decimal_t<int64_t, 2> money{3.50};
    fixed_decimal_t<int64_t, 4> precise{money};   // 2 -> 4, lossless
    EXPECT_EQ(precise.raw(), 35000);              // 3.5000
    EXPECT_DOUBLE_EQ(precise.to<double>(), 3.50);
}

TEST(Convert, EqualPrecisionIsIdentity) {
    fixed_decimal_t<int64_t, 3> a{1.234};
    fixed_decimal_t<int64_t, 3> b{a};
    EXPECT_EQ(a.raw(), b.raw());
}

// Narrowing uses the [[deprecated]] ctor — still correct, just warns.
// We locally silence the warning to assert the runtime behavior.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
TEST(Convert, NarrowingTruncates) {
    fixed_decimal_t<int64_t, 4> precise{3.1416};
    fixed_decimal_t<int64_t, 2> narrowed{precise};  // 4 -> 2
    EXPECT_EQ(narrowed.raw(), 314);                  // 3.14 (truncated)
}
#pragma GCC diagnostic pop


// ============================================================================
// Limits
// ============================================================================

TEST(Limits, Int64P2) {
    using F = fixed_decimal_t<int64_t, 2>;
    EXPECT_EQ(F::raw_max(), std::numeric_limits<int64_t>::max());
    EXPECT_EQ(F::raw_min(), std::numeric_limits<int64_t>::min());
    EXPECT_EQ(F::max().raw(), std::numeric_limits<int64_t>::max());
    EXPECT_EQ(F::min().raw(), std::numeric_limits<int64_t>::min());
    EXPECT_EQ(F::max_integer(), std::numeric_limits<int64_t>::max() / 100);
}

TEST(Limits, NumericLimitsSpecialization) {
    using F = fixed_decimal_t<int64_t, 2>;
    EXPECT_TRUE(std::numeric_limits<F>::is_specialized);
    EXPECT_EQ(std::numeric_limits<F>::max().raw(), F::max().raw());
    EXPECT_EQ(std::numeric_limits<F>::lowest().raw(), F::min().raw());
    EXPECT_EQ(std::numeric_limits<F>::epsilon().raw(), 1);
}

// ============================================================================
// Overflow safety
// ============================================================================

TEST(Overflow, MultiplyDoesNotThrowOnOverFlow) {
    fixed_decimal_t<int32_t, 2> big{200000.0};   // 20,000,000 stored
    EXPECT_NO_THROW({ auto r = big * big; (void)r; });
}
//
// TEST(Overflow, AddThrowsAtCeiling) {
//     using F = fixed_decimal_t<int32_t, 2>;
//     F top = F::max();
//     F one{1};
//     EXPECT_THROW({ auto r = top + one; (void)r; }, std::overflow_error);
// }
//
// TEST(Overflow, SubtractThrowsAtFloor) {
//     using F = fixed_decimal_t<int32_t, 2>;
//     F bottom = F::min();
//     F one{1};
//     EXPECT_THROW({ auto r = bottom - one; (void)r; }, std::overflow_error);
// }

// ============================================================================
// Stream formatting
// ============================================================================

TEST(Format, BasicTwoPlaces) {
    EXPECT_EQ(Str(fixed_decimal_t<int32_t, 2>{292.09}), "292.09");
}
TEST(Format, PadsLeadingZeroInFraction) {
    EXPECT_EQ(Str(fixed_decimal_t<int32_t, 2>{0.07}), "0.07");
}
TEST(Format, Negative) {
    EXPECT_EQ(Str(fixed_decimal_t<int32_t, 2>{-4.30}), "-4.30");
}
TEST(Format, PrecisionZeroHasNoPoint) {
    EXPECT_EQ(Str(fixed_decimal_t<int32_t, 0>{42}), "42");
}
TEST(Format, MostNegativeValueDoesNotOverflow) {
    // Regression: negating INT64_MIN in operator<< used to print garbage.
    using F = fixed_decimal_t<int32_t, 2>;
    EXPECT_EQ(Str(F::min()), "-92233720368547758.08");
}
TEST(Format, HighPrecisionPadding) {
    EXPECT_EQ(Str(fixed_decimal_t<int32_t, 4>{3.5}), "3.5000");
}

// ============================================================================
// Float round-trip (bounded: floats only hold ~7 significant digits)
// ============================================================================

TEST(FloatRoundTrip, SmallValuesWithinEpsilon) {
    const std::vector<float> vals{0.0f, 1.1f, 2.5f, 10.25f, -3.75f};
    for (float v : vals) {
        fixed_decimal_t<int64_t, 4> f{v};
        EXPECT_NEAR(f.to<float>(), v, 1e-3f)
            << "float round trip drift for " << v;
    }
}

} // namespace dsl::test