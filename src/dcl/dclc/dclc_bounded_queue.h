//
// Created by Dominic Kloecker on 16/09/2026.
//

#ifndef DSL_DCLC_BOUNDED_QUEUE_H
#define DSL_DCLC_BOUNDED_QUEUE_H

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

#include "dslu_concepts.h"
namespace dcl {

// TODO: Add utility to take concurrency and assert min / max level onto it.
// E.g. to allow for adding methods and constricting them based on concurrency
// e.g. this method can only be used when MC safe˙
// example is a peek_front() / peek_back() method to peek but not pop. But i might only want this
// for the sp/sc versions of them
enum class concurrency : std::uint8_t {
	NONE = 0,			// 0000
	SC   = 1 << 0,		// 0001
	SP	 = 1 << 1,		// 0010
	MC   = SC | 1 << 2,	// 0101
	MP	 = SP | 1 << 3, // 1010
	SPSC = SP | SC,		// 0011
	SPMC = SP | MC,		// 0111
	MPSC = MP | SC,		// 1011
	MPMC = MP | MC		// 1111
};

namespace details {


/// Signed distance between two sequence numbers (safe for underflow)
constexpr std::ptrdiff_t diff(const std::size_t l, const std::size_t r) {
	return static_cast<std::ptrdiff_t>(l-r);
}

inline constexpr size_t cache_line = 64;

/// Dynamic extend used to determine non static allocation (i.e. size at runtime)
template <std::size_t N>
inline constexpr bool is_dynamic = (N == std::dynamic_extent);

/// Valid extend is either power of 2 or dynamic extend
template <std::size_t N>
concept valid_or_dynamic = is_dynamic<N> || dsl::power_of_two<N>;

// Ensures that the requested queue concurrency model is supported (having both a consumer and producer policy defined)
// Must have a consumer (0001) and a producer (0010)

// MC includes MC and SC Flag, at least one of the two must be set.
template <concurrency C>
concept has_consumer_policy = (static_cast<std::uint8_t>(concurrency::MC) & static_cast<std::uint8_t>(C)) > 0;

// MP includes MP and SP Flag, at least one of the two must be set.
template <concurrency C>
concept has_producer_policy= (static_cast<std::uint8_t>(concurrency::MP) & static_cast<std::uint8_t>(C)) > 0;

template <concurrency C>
concept valid_concurrency_model = has_consumer_policy<C> && has_producer_policy<C>;


template <typename T>
struct byte_slot {
	alignas(T) std::array<std::byte, sizeof(T)> buffer_;

	T* ptr()				{ return std::launder(reinterpret_cast<T*>(buffer_.data())); }
	const T* ptr() const	{ return std::launder(reinterpret_cast<const T*>(buffer_.data())); }

	T& value()				{ return *ptr(); }
	const T& value() const	{ return *ptr(); }

	/// @warning undefined behaviour if slot has already been claimed
	template<typename ...Args>
	T* emplace(Args&&...args) {
		return new (static_cast<void*>(buffer_.data())) T(std::forward<Args>(args)...);
	}

	/// @warning undefined behaviour if slot is not currently claimed
	void destroy() { ptr()->~T(); }
};

template <typename T>
struct seq_slot : byte_slot<T> {
	std::atomic_size_t seq_{0};
};

// Static sized buffer (size part of type)
template <typename Slot, std::size_t N>
	requires valid_or_dynamic<N>
class ring_buffer {
	static_assert(dsl::power_of_two<N>, "Ring size must be a power of two");
	std::array<Slot, N> ring;
public:
	/// Capacity of the Ring Buffer
	constexpr static std::size_t capacity() {return N;}
	/// Wrap around safe index from a monotonic increasing sequence counter
	constexpr static std::size_t index(const std::size_t seq) {return seq & (N-1);}
	/// Accessor of the Slot at the provided index in the buffer.
	Slot& operator[](const std::size_t index)			  noexcept	{return ring[index];}
	const Slot& operator[](const std::size_t index) const noexcept	{return ring[index];}
};

// TODO: Maybe allow for wrap around policy so that we can have different implementation when not power of two

// Dynamic sized buffer (size not part of type)
template <typename Slot>
class ring_buffer<Slot, std::dynamic_extent> {
	const std::size_t mask;
	// MPMC queue requires atomic sequence counter.
	// Since the size is not known at compile time, and atomic is not movable
	std::unique_ptr<Slot[]> ring;

	static std::size_t create_mask(const std::size_t requested) {
		// Enforce that requested is valid power of 2
		if (!std::has_single_bit(requested))
			throw std::invalid_argument("Ring Size must be a power of 2");
		return requested - 1;
	}
public:
	explicit ring_buffer(const std::size_t capacity)
		: mask(create_mask(capacity))
		, ring(std::make_unique<Slot[]>(capacity)) {}

	/// Capacity of the Ring Buffer
	[[nodiscard]] std::size_t capacity() const noexcept {return mask + 1;}
	/// Wrap around safe index from a monotonic increasing sequence counter
	[[nodiscard]] std::size_t index(const std::size_t seq) const noexcept {return seq & mask;}
	/// Accessor of the Slot at the provided index in the buffer.
	Slot& operator[](const std::size_t index)			  noexcept	{return ring[index];}
	const Slot& operator[](const std::size_t index) const noexcept	{return ring[index];}
};

template <typename Slot, std::size_t N>
class queue_base {
	// Alignment of buffer holding data equal to maximum of cache line or the container holding the data
	static constexpr std::size_t buff_alignment = std::max(cache_line, alignof(ring_buffer<Slot, N>));
protected:
	// Protected (not private) so the concrete algorithms deriving from this can reach them
	alignas(cache_line) std::atomic_size_t		tail_{0};
	alignas(cache_line) std::atomic_size_t		head_{0};
	// Ensure alignment of ring buffer slot in case element is aligned over cache_line.
	alignas(std::max(cache_line, alignof(ring_buffer<Slot, N>))) ring_buffer<Slot, N> ring_buffer_;
	// cha
public:
	/// Pick compile time for static size
	queue_base() requires (!is_dynamic<N>) = default;

	/// Run time for dynamic size
	explicit queue_base(const std::size_t size)
	requires is_dynamic<N> : ring_buffer_(size) {}

	/// On destruction drain destroy all remaining elements in the buffer
	~queue_base() {
		// relaxed memory order as destruction is not safe if queue is still in use
		const std::size_t t = tail_.load(std::memory_order::relaxed);
		// Destroy all elements until head is back on top of tail
		for (std::size_t h = head_.load(std::memory_order::relaxed);
			 h != t; h++) {
			ring_buffer_[ring_buffer_.index(h)].destroy();
		}
	}

	/// Capacity check of buffer size
	std::size_t capacity() const
	requires is_dynamic<N>	  {return ring_buffer_.capacity();}

	constexpr static std::size_t capacity()
	requires (!is_dynamic<N>) {return ring_buffer<Slot, N>::capacity();}

	/// Approximate number of elements in the queue (non reliable)
	std::size_t size() const {
		// Check distance between tail and head
		const std::ptrdiff_t dist = diff(
			tail_.load(std::memory_order::acquire),
			head_.load(std::memory_order::acquire));
		// if head is ahead of tail (likely lapped) approximate as 0
		return dist < 0 ? 0 : static_cast<std::size_t>(dist);
	}

	/// Approximate check if queue is currently empty (non reliable)
	bool empty() const { return size() == 0; }

	/// Approximate check if queue is currently full (non reliably)
	bool full() const { return size() == capacity(); }

	/// Approximation of number of remaining write available
	std::size_t available() const { return capacity() - size();}
};

/// Bounded SPSC queue can use basic byte slot
template <typename T, size_t N>
class bounded_spsc_imp : public queue_base<byte_slot<T>, N> {
	using base = queue_base<byte_slot<T>, N>;
	// Bring dependent base members into scope
	using base::tail_;
	using base::head_;
	using base::ring_buffer_;
public:
	// Inherit base constructor (for dynamic size)
	using base::base;

	template <typename ...Args>
	bool produce(Args&&...args) {
		const size_t curr = tail_.load(std::memory_order::relaxed);
		// if tail is ahead of head by capacity then queue is full and we cannot produce
		if (curr - head_.load(std::memory_order_acquire)
			>= ring_buffer_.capacity()) return false;
		// else place value in slot
		ring_buffer_[ring_buffer_.index(curr)].emplace(std::forward<Args>(args)...);
		tail_.store(curr + 1, std::memory_order_release);
		return true;
	}

	bool consume(T& fill) {
		const size_t curr = head_.load(std::memory_order_relaxed);
		// if head sits on top of the tail then the queue is empty and we cannot consume
		if (tail_.load(std::memory_order_acquire) - curr == 0) return false;
		// else move value from slot into provided element and destroy the moved element
		auto & slot = ring_buffer_[ring_buffer_.index(curr)];
		fill = std::move(slot.value());
		slot.destroy();
		head_.store(curr + 1, std::memory_order_release);
		return true;
	}
};

/// Bounded MPMC queue requires sequence slots
template <typename T, size_t N>
class bounded_mpmc_imp : public queue_base<seq_slot<T>, N> {
	using base = queue_base<seq_slot<T>, N>;
	// Bring dependent base members into scope
	using base::tail_;
	using base::head_;
	using base::ring_buffer_;

	void init_slot_sequence() {
		// Set the initial slot values (equal to their index)
		for (size_t i = 0; i != ring_buffer_.capacity(); i++) {
			ring_buffer_[i].seq_.store(i, std::memory_order_relaxed);
		}
	}
public:
	// MPMC Queue must init the sequence numbers in each slot before it can be used
	bounded_mpmc_imp()
	requires (!is_dynamic<N>)
		{ init_slot_sequence(); }

	explicit bounded_mpmc_imp(const size_t capacity)
	requires is_dynamic<N>
		: base(capacity)
		{ init_slot_sequence(); }

	// TODO: Update produce and consume algos to be flexible?
	// E.g. on fail / on success policy? could then even allow for returning of values

	template <typename ...Args>
	requires std::is_nothrow_constructible_v<T, Args...> || std::is_nothrow_move_constructible_v<T>
	bool produce(Args&&...args) {
		// Element constructor might throw (e.g. bad alloc), once a producer has claimed it.
		// Since this would then prevent the publishing of the sequence count to the claimed state
		// leaving queue unrecoverable, we attempt construction of the element itself before undergoing the claim
		// the claim and emplacement step, which should ensure that the element would have already thrown
		if constexpr (!std::is_nothrow_constructible_v<T, Args...>) {
			T staged(std::forward<Args>(args)...);
			return claim_and_emplace(std::move(staged));
		} else {
			return claim_and_emplace(std::forward<Args>(args)...);
		}
	}

private:
	template <typename ...Args>
	requires std::is_nothrow_constructible_v<T, Args...>
	bool claim_and_emplace(Args&&...args) {
		size_t pos = tail_.load(std::memory_order_relaxed);
		seq_slot<T> * slot;
		// To claim a slot we need to slot to be at sequence number = tail
		// and write tail + 1 once value is stored (to mark it safe for consumption)
		while (true) {
			slot = &ring_buffer_[ring_buffer_.index(pos)];
			// If the position is the same value as the slot sequence counter
			// the slot has not yet been claimed, so we can attempt to claim
			if (const std::ptrdiff_t d = diff(slot->seq_.load(std::memory_order_acquire), pos);
				d == 0) {
				// Attempt to claim slot by advancing tail to next position.
				// If we fail to claim tail, we have been beaten by another thread so retry on new position
				if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) break;
			} else if (d < 0) {
				// Slot still contains the element from the last lap (queue is full) so fail
				return false;
			} else {
				// Another producer has already claimed this slot, try again with updated position
				pos = tail_.load(std::memory_order_relaxed);
			}
		}
		// Store data and publish claim
		slot->emplace(std::forward<Args>(args)...);
		slot->seq_.store(pos + 1, std::memory_order_release);
		return true;
	}

public:
	bool consume(T& fill) {
		std::size_t pos = head_.load(std::memory_order_relaxed);
		seq_slot<T> * slot;
		// A slot is viable for consumption if its sequence is = head + 1
		// Once consumed, it is marked as ready for storing of a new element via head + capacity (i.e. next lap)
		for (;;) {
			slot = &ring_buffer_[ring_buffer_.index(pos)];
			if (const std::ptrdiff_t d = diff(slot->seq_.load(std::memory_order_acquire), pos + 1);
				d == 0) {
				// Attempt to claim slot of re-try if another thread has beaten us
				if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) break;
			} else if (d < 0) {
				// There is nothing to consume (queue is empty) so fail
				return false;
			} else {
				// Another consumer has already claimed this slot so retry
				pos = head_.load(std::memory_order_relaxed);
			}
		}
		// Move element out of slot and reset it
		fill = std::move(slot->value());
		slot->destroy();
		// Publish completion of slot transfer
		slot->seq_.store(pos + ring_buffer_.capacity(), std::memory_order_release);
		return true;
	}
};

/// Each Queue Algorithm must implement consume and produce methods
template <typename Q, typename T, typename ...Args>
concept bounded_queue_imp = requires(Q& q, T& out, Args&&...args) {
	{q.consume(out)							}	-> std::same_as<bool>;
	{q.produce(std::forward<Args>(args)...) }	-> std::same_as<bool>;
};

// Policy selection: maps a concurrency model onto an algorithm.
// Every specialisation exposes the chosen implementation as `type`.

// Default choice is MPMC since it is the safest (covers SPMC and MPSC until
// dedicated algorithms exist; add a specialisation below when they do)
template <typename T, concurrency C, size_t N>
struct queue_for { using type = bounded_mpmc_imp<T, N>; };

template <typename T, size_t N>
struct queue_for<T, concurrency::SPSC, N> { using type = bounded_spsc_imp<T, N>; };

template <typename T, concurrency C, size_t N>
using queue_t = queue_for<T, C, N>::type;

}

/**
 * @breif Bounded thread safe, lock free queue.
 * Queue of a fixed size (implemented via a ring buffer), that supports a consumer producer model.
 * @tparam T  Object Type to be placed into the queue
 * @tparam C  Requested minimum concurrency model
 * @tparam N  Queue Capacity (if N == std::dynamic_extend, the queue capacity is a runtime variable)
 *
 * Will automatically determine the appropriate lock free algorithm based on the desired concurrency model.
 * Guaranteeing that the queue will be able to at least serve the number of consumers / producer threads requested
 *
 * The provided Capacity will determine the underlying storage model.
 * bounded_queue<T, C, 8>	-> Compile time sizing and size enforcement
 * bounded_queue<T, C>(N)	-> Runtime size enforcement over constructor
 */
template <typename T, concurrency C, size_t N = std::dynamic_extent>
	requires details::valid_concurrency_model<C> && details::valid_or_dynamic<N>
class bounded_queue : details::queue_t<T, C, N> {
	// Inherited Based Queue Type implementing the consumer producer algo
	using queue_t = details::queue_t<T, C, N>;
	static_assert(details::bounded_queue_imp<queue_t, T, const T&>, "Selected queue algorithm is invalid");

	using queue_t::consume;
	using queue_t::produce;
public:
	using queue_t::queue_t;
	using queue_t::size;
	using queue_t::capacity;
	using queue_t::empty;
	using queue_t::full;

	using value_t = T;

	/// Concurrency Model of Queue
	static constexpr concurrency model = C;
	static constexpr bool		 lock_free = true;
	static constexpr bool		 thread_safe = true;

	/**
	 * Attempt to create a new item and insert it at the back of the queue
	 * @return true on success (object was created in queue), false if the queue is full (No-Op)
	 */
	template <typename ...Args>
	requires std::constructible_from<T, Args...>
	bool emplace(Args&&...args) { return produce(std::forward<Args>(args)...); }

	/**
	 * Attempt to push a new item onto the queue.
	 * @return true on success (object was pushed onto queue), false if the queue is full (No-Op)
	 */
	bool push(const T& in) { return produce(in); }
	bool push(T&& in) { return produce(std::move(in)); }

	/**
	 * Attempts to pop the oldest item from the queue into the fill object
	 * @return true on success (object was filled), false if the queue is empty (No-Op)
	 */
	bool pop(T& fill) { return consume(fill); }

	/** @return The moved front element, or std::nullopt if the queue is empty. */
	std::optional<T> try_pop()
	requires std::is_default_constructible_v<T>
	{
		if (T fill{}; pop(fill)) {
			return fill;
		}
		return std::nullopt;
	}


	/**
	 * Attempt to consume element from queue and move it onto the output iterator
	 * @return true if element popped, false otherwise
	 */
	template <std::output_iterator<T> O>
	bool pop_onto(O it) {
		return pop(*(it++));
	}

	/**
	 * Consume one element via the provided function
	 * @param fn function to apply onto the dequed element
	 * @return true if elements was consumed, false otherwise
	 * @remarks ThreadSafe and Non-Blocking only if provided function is
	 */
	template <typename Function>
	requires std::is_invocable_v<Function, T>
	bool consume_one(Function&& fn) {
		const auto consumed = try_pop();
		if (!consumed.has_value()) return false;
		std::invoke(std::forward<Function>(fn), std::move(consumed.value()));
		return true;
	}

	/**
	 * Consume as many (up to all) elements from the queue sequentially
	 * and apply provided function to each object
	 * @param fn to apply onto each consumed element
	 * @return the number of elements consumed and processed via function
	 * @remarks ThreadSafe and Non-Blocking only if provided function is
	 */
	template <typename Function>
	requires std::is_invocable_v<Function, T>
	std::size_t consume_all_available(Function&& fn) {
		std::size_t num_processed = 0;
		for (;;) {
			std::optional<T> consumed = try_pop();
			// if consumed is empty we are done
			if (!consumed) break;
			std::invoke(fn, std::move(consumed.value()));
			++num_processed;
		}
		return num_processed;
	}

	/**
	 * Resets the queue by draining and discarding all remaining elements
	 * @warning if queue still actively has elements produced into it, full reset is not guaranteed
	 */
	void reset() {
		// TODO: Might want to only go through destruction when no trivially destructible (like boost)
		// We may actually want to
		consume_all_available([](const T&){});
	}

	// TODO: Add generator drain version in future?
	// std::<T> drain() requires std::default_initializable<T> {
	// 	T v;
	// 	while (consume(v)) co_yield std::move(v);
	// }
};

// Convenient Type Aliases
template <typename T, std::size_t N = std::dynamic_extent>
using b_spsc_q = bounded_queue<T, concurrency::SPSC, N>;

// TODO: Implement algo
template <typename T, std::size_t N = std::dynamic_extent>
using b_spmc_q = bounded_queue<T, concurrency::SPMC, N>;

// TODO: Implement algo
template <typename T, std::size_t N = std::dynamic_extent>
using b_mpsc_q = bounded_queue<T, concurrency::MPSC, N>;

template <typename T, std::size_t N = std::dynamic_extent>
using b_mpmc_q = bounded_queue<T, concurrency::MPMC, N>;

template <typename Q, typename T>
concept bounded_queue_like = requires (Q& q, const T& in, T& out) {
	// Operations
	{ q.push(in) }		-> std::same_as<bool>;
	{ q.pop(out) }		-> std::same_as<bool>;
	// Queue statistics
	{ q.capacity() }	-> std::convertible_to<std::size_t>;
	{ q.size() }		-> std::convertible_to<std::size_t>;
	{ q.full() }		-> std::same_as<bool>;
	{ q.empty() }		-> std::same_as<bool>;
	// TODO: Add a nice way to define concurrency traits
	// // Concurrency model
	// { Q::model }		-> std::convertible_to<concurrency>;
	// { Q::lock_free }	-> std::convertible_to<bool>;
	// { Q::thread_safe }  -> std::convertible_to<bool>;
};

}

#endif //DSL_DCLC_BOUNDED_QUEUE_H