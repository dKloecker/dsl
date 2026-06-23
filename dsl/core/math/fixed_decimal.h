//
// Created by Dominic Kloecker on 16/06/2026.
//

#ifndef DSL_FIXED_DECIMAL_H
#define DSL_FIXED_DECIMAL_H
#include <algorithm>

#include "dsl/core/concepts/dsl_concepts.h"
#include "dsl/core/utils/dsl_util.h"

namespace dsl {
// TODO: Maybe Add a Safe Version that widens?
//
// template<std::integral T>
// struct widen {
// 	using type = std::int64_t;
// };
//

// https://en.wikipedia.org/wiki/Fixed-point_arithmetic

// TODO: docs
template<std::integral T, std::size_t P>
	requires std::is_signed_v<T> && container_supports_precision<T, P>
class fixed_decimal_t {
	static_assert(std::is_signed_v<T>,
	              "fixed_decimal_t requires a signed integral storage type");
	static_assert(P <= static_cast<std::size_t>(std::numeric_limits<T>::digits10),
		"provided precision must be within the representable bounds of the storage type");

	template<std::integral A, std::size_t B>
	requires std::is_signed_v<A> && container_supports_precision<A, B>
	friend class fixed_decimal_t;

public:
	using wrapped_t                          = T;
	static constexpr std::size_t Precision   = P;
	static constexpr wrapped_t   ScaleFactor = power_of<10, P>();

private:
	wrapped_t value_{};

	struct raw_tag {};

	constexpr fixed_decimal_t(wrapped_t raw, raw_tag) noexcept : value_(raw) {}

public:
	constexpr fixed_decimal_t() noexcept                        = default;
	constexpr fixed_decimal_t(const fixed_decimal_t &) noexcept = default;
	constexpr fixed_decimal_t(fixed_decimal_t &&) noexcept      = default;

	fixed_decimal_t &operator=(const fixed_decimal_t &) noexcept = default;
	fixed_decimal_t &operator=(fixed_decimal_t &&) noexcept      = default;
	~fixed_decimal_t()                                           = default;

	template<std::integral I>
	explicit constexpr fixed_decimal_t(I v)
		: value_(static_cast<wrapped_t>(v) * ScaleFactor) {}

	// TODO: Revisit
	template<std::floating_point F>
	explicit constexpr fixed_decimal_t(F v)
	// round half away from zero rather than truncating toward zero
		: value_(static_cast<wrapped_t>(
			v * static_cast<F>(ScaleFactor) + (v >= F(0) ? F(0.5) : F(-0.5)))) {}

	template<std::size_t PO>
		requires (PO <= P) // widening or equal: always safe
	explicit constexpr fixed_decimal_t(const fixed_decimal_t<T, PO> &o)
		: value_(o.value_ * (ScaleFactor / fixed_decimal_t<T, PO>::ScaleFactor)) {}

	template<std::size_t PO>
		requires (PO > P) // narrowing: precision loss
	[[deprecated("narrowing fixed_decimal_t to fewer fractional digits loses precision")]]
	explicit constexpr fixed_decimal_t(const fixed_decimal_t<T, PO> &o)
		: value_(o.value_ / (fixed_decimal_t<T, PO>::ScaleFactor / ScaleFactor)) {}

	template<std::integral R>
	constexpr R to() const { return static_cast<R>(value_ / ScaleFactor); }

	template<std::floating_point R>
	constexpr R to() const {
		return static_cast<R>(value_) / static_cast<R>(ScaleFactor);
	}

	constexpr wrapped_t  raw() const noexcept { return value_; }
	constexpr wrapped_t &raw() noexcept { return value_; }

	constexpr fixed_decimal_t operator+(const fixed_decimal_t &o) const {
		return {static_cast<wrapped_t>(value_ + o.value_), raw_tag{}};
	}

	constexpr fixed_decimal_t operator-(const fixed_decimal_t &o) const {
		return {static_cast<wrapped_t>(value_ - o.value_), raw_tag{}};
	}

	// multiplication: (a·S)(b·S) = ab·S^2, so divide one S back out
	constexpr fixed_decimal_t operator*(const fixed_decimal_t &o) const {
		return {static_cast<wrapped_t>(value_ * o.value_ / ScaleFactor), raw_tag{}};
	}

	// division: (a·S)/(b·S) = a/b, so multiply numerator by S first to keep scale
	constexpr fixed_decimal_t operator/(const fixed_decimal_t &o) const {
		return {static_cast<wrapped_t>(value_ * ScaleFactor / o.value_), raw_tag{}};
	}

	// unary minus
	constexpr fixed_decimal_t operator-() const {
		return {static_cast<wrapped_t>(-value_), raw_tag{}};
	}

	constexpr fixed_decimal_t &operator+=(const fixed_decimal_t &o) {
		value_ += o.value_;
		return *this;
	}

	constexpr fixed_decimal_t &operator-=(const fixed_decimal_t &o) {
		value_ -= o.value_;
		return *this;
	}

	constexpr fixed_decimal_t &operator*=(const fixed_decimal_t &o) {
		value_ = value_ * o.value_ / ScaleFactor;
		return *this;
	}

	constexpr fixed_decimal_t &operator/=(const fixed_decimal_t &o) {
		value_ = value_ * ScaleFactor / o.value_;
		return *this;
	}

	constexpr fixed_decimal_t &operator++() {
		value_ += ScaleFactor;
		return *this;
	}

	constexpr fixed_decimal_t &operator--() {
		value_ -= ScaleFactor;
		return *this;
	}

	constexpr fixed_decimal_t operator++(int) {
		auto old = *this;
		value_   += ScaleFactor;
		return old;
	}

	constexpr fixed_decimal_t operator--(int) {
		auto old = *this;
		value_   -= ScaleFactor;
		return old;
	}

	constexpr auto operator<=>(const fixed_decimal_t &o) const = default;
	constexpr bool operator==(const fixed_decimal_t &o) const  = default;

	// TODO: Maybe allow cross precision if with smaller?
	template<std::size_t PO> requires (PO != P)
	void operator+(const fixed_decimal_t<T, PO> &) const = delete;

	template<std::size_t PO> requires (PO != P)
	void operator-(const fixed_decimal_t<T, PO> &) const = delete;

	template<std::size_t PO> requires (PO != P)
	void operator*(const fixed_decimal_t<T, PO> &) const = delete;

	template<std::size_t PO> requires (PO != P)
	void operator/(const fixed_decimal_t<T, PO> &) const = delete;

	constexpr fixed_decimal_t abs() const {
		return {static_cast<wrapped_t>(value_ < 0 ? -value_ : value_), raw_tag{}};
	}

	// TODO: Numeric limit overrides https://cppreference.com/cpp/types/numeric_limits

	static constexpr fixed_decimal_t max() noexcept {
		return {std::numeric_limits<wrapped_t>::max(), raw_tag{}};
	}

	static constexpr fixed_decimal_t min() noexcept {
		return {std::numeric_limits<wrapped_t>::min(), raw_tag{}};
	}

	static constexpr wrapped_t max_integer() noexcept {
		return std::numeric_limits<wrapped_t>::max() / ScaleFactor;
	}

	static constexpr wrapped_t min_integer() noexcept {
		return std::numeric_limits<wrapped_t>::min() / ScaleFactor;
	}

	static constexpr wrapped_t raw_max() noexcept {
		return std::numeric_limits<wrapped_t>::max();
	}

	static constexpr wrapped_t raw_min() noexcept {
		return std::numeric_limits<wrapped_t>::min();
	}

	template<std::integral, std::size_t>
	friend std::ostream &operator<<(std::ostream &os, const fixed_decimal_t &o) {
		if constexpr (P == 0) {
			os << o.value_;
		} else {
			wrapped_t v = o.value_;
			if (v < 0) {
				os << '-';
				v = -v;
			}
			wrapped_t   whole = v / ScaleFactor;
			wrapped_t   frac  = v % ScaleFactor;
			std::string f     = std::to_string(frac);
			// pad leading zeros
			if (f.size() < P) f.insert(0, P - f.size(), '0');
			os << whole << '.' << f;
		}
		return os;
	}
};

template<std::integral A, std::size_t B>
std::ostream &operator<<(std::ostream &os, const fixed_decimal_t<A, B> &o) {
	os << o;
	return os;
}

template<std::size_t P>
using
fixed8 = fixed_decimal_t<std::int8_t, P>;

template<std::size_t P>
using
fixed16 = fixed_decimal_t<std::int16_t, P>;

template<std::size_t P>
using
fixed32 = fixed_decimal_t<std::int32_t, P>;

template<std::size_t P>
using
fixed64 = fixed_decimal_t<std::int64_t, P>;

}

#endif //DSL_FIXED_DECIMAL_H
