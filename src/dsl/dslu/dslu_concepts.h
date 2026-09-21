//
// Created by Dominic Kloecker on 25/05/2026.
//

#ifndef DSL_DSLU_CONCEPTS_H_
#define DSL_DSLU_CONCEPTS_H_

#include <functional>
#include <limits>
#include <span>

namespace dsl {
template<typename T>
concept hashable = requires(const T &t)
{
	{ std::hash<T>{}(t) } -> std::convertible_to<std::size_t>;
};

template<size_t Value>
concept power_of_two = (Value > 0) && ((Value & (Value - 1)) == 0);


template <size_t Base>
constexpr bool is_power_of(size_t val) {
	static_assert(Base > 1, "Base must be greater than 1");
	if (val == 0) return false;
	while (val % Base == 0) {
		val /= Base;
	}
	return val == 1;
}

template<size_t Base, size_t Val>
concept is_power_of_v = is_power_of<Base>(Val);

template<typename T, size_t P>
concept container_supports_precision =
	(P <= static_cast<std::size_t>(std::numeric_limits<T>::digits10));

template <const std::size_t N>
concept is_dynamic_extend = N == std::dynamic_extent;

};

#endif  // DSL_DSLU_CONCEPTS_H_
