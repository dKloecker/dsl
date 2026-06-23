//
// Created by Dominic Kloecker on 31/03/2026.
//

#ifndef DSL_UTIL_H
#define DSL_UTIL_H
#include <cstddef>

namespace dsl {
constexpr size_t align_up(const size_t size, const size_t alignment) {
    return ((size + alignment - 1) / alignment) * alignment;
}

constexpr size_t round_up_pow2(size_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return v + 1;
}

template <size_t N, size_t E>
constexpr size_t power_of() {
	if constexpr (E == 0) {
		return 1;
	} else {
		return N * power_of<N, E - 1>();
	}
}

}

#endif //DSL_UTIL_H
