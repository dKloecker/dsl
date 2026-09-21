//
// Created by Dominic Kloecker on 19/09/2026.
//

#ifndef DSL_DCLU_CONCEPTS_H
#define DSL_DCLU_CONCEPTS_H
#include <chrono>
#include <concepts>
#include <cstdint>

namespace dcl {

template <typename L>
concept is_basic_lockable = requires(L& l) {
	{ l.lock() }		-> std::same_as<void>;
	{ l.unlock() }		-> std::same_as<void>;
};

template <typename L>
concept is_lockable = is_basic_lockable<L> && requires(L& l) {
	{ l.try_unlock() }	-> std::same_as<bool>;
};

// TODO:  Add timed lockable
// template <typename L>
// concept is_timed_lockable = is_basic_lockable<L> && requires(
// 	L& l, std::chrono::duration<double> rel_time,  std::chrono::time_point<std::chrono::system_clock> abs_time) {
// 	{ l.try_lock_for(rel_time)		} -> std::same_as<bool>;
// 	{ l.try_lock_until(abs_time)	} -> std::same_as<bool>;
// };
}

#endif //DSL_DCLU_CONCEPTS_H
