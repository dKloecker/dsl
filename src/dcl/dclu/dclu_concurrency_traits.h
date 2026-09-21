//
// Created by Dominic Kloecker on 21/09/2026.
//

#ifndef DSL_DCLU_CONCURRENCY_TRAITS_H
#define DSL_DCLU_CONCURRENCY_TRAITS_H

#include <concepts>
#include <cstdint>
#include <utility>

namespace dcl {

/**
 * Producer Consumer Safety Enum
 */
enum class concurrency : std::uint8_t {
	none	= 0,			/* 0000 */
	sr		= 1 << 0,		/* 0001 */
	sw		= 1 << 1,		/* 0010 */
	mr		= sr | 1 << 2,  /* 0101 */
	mw		= sw | 1 << 3,  /* 1010 */
	swsr	= sw | sr,		/* 0011 */
	swmr	= sw | mr,		/* 0111 */
	mwsr	= mw | sr,		/* 1011 */
	mwmr	= mw | mr,		/* 1111 */
};

/**
 * Determines whether the provided concurrency level is satisfied by the required level
 * I.e. whether the provide is at least a complete subset of the required
 */
constexpr bool provides(const concurrency have, const concurrency need) {
	const auto h = std::to_underlying(have);
	const auto n = std::to_underlying(need);
	return (h & n) == n;
}

template <concurrency Have, concurrency Need>
concept is_safe_for = provides(Have, Need);

/// Only consider a valid model if atleast a writer and reader policy is defined
template <concurrency C>
concept is_valid_model = is_safe_for<C, concurrency::swsr>;

template <concurrency C>
concept multi_writer = is_safe_for<C, concurrency::mw>;

template <concurrency C>
concept single_writer = !multi_writer<C>;

template <concurrency C>
concept multi_reader = is_safe_for<C, concurrency::mr>;

template <concurrency C>
concept single_reader = !multi_reader<C>;

/**
 * A Type has a Concurrency Guarantee if it has a
 * static concurrency_guarantee member
 */
template <typename T>
concept has_concurrency_guarantee = requires {
	{ T::concurrency_guarantee } -> std::same_as<const concurrency&>;
};

// TODO: Lock free traits
// template <typename T>
// concept has_lock_guarantee = requires {
// 	{ T::is_lock_free }			-> std::same_as<bool>;
// };

template <concurrency Level>
struct basic_concurrency_traits {
	static constexpr bool		 is_specialized = true;
	static constexpr concurrency guarantees = Level;

	static constexpr bool is_sr_safe   = provides(Level, concurrency::sr);
	static constexpr bool is_sw_safe   = provides(Level, concurrency::sw);
	static constexpr bool is_mr_safe   = provides(Level, concurrency::mr);
	static constexpr bool is_mw_safe   = provides(Level, concurrency::mw);
	static constexpr bool is_mwmr_safe = provides(Level, concurrency::mwmr);
};


// unspecialized versions has no guarantee
template <typename T>
struct concurrency_traits : basic_concurrency_traits<has_concurrency_guarantee<T>
		? T::concurrency_guarantee
		: concurrency::none>
{
	// Check if there is a concurrency guarantee provided
	static constexpr bool is_specialized = false;
};

}
#endif //DSL_DCLU_CONCURRENCY_TRAITS_H
