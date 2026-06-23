//
// Created by Dominic Kloecker on 25/05/2026.
//

#ifndef DSL_CONCEPTS_H
#define DSL_CONCEPTS_H

#include <functional>

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

};

#endif //DSL_CONCEPTS_H
