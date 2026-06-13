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
}

#endif //DSL_CONCEPTS_H
