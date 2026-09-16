//
// Created by Dominic Kloecker on 09/08/2026.
//

#ifndef DSL_DCLC_MPMC_BOUNDED_QUEUE_H_
#define DSL_DCLC_MPMC_BOUNDED_QUEUE_H_

#include <atomic>
#include <cmath>
#include <new>
#include <optional>
#include <mutex>
#include <shared_mutex>

#include "dslu_concepts.h"
#include "dslu_util.h"

namespace dcl {

template <typename T, size_t Capacity>
    requires (dsl::power_of_two<Capacity> && Capacity > 1)
class mpmc_bounded_queue_imp {
    using value_type = T;
    static constexpr size_t ELEMENT_SIZE = sizeof(T);
	static constexpr size_t MASK = Capacity - 1;

    struct Slot {
        alignas(T) std::array<std::byte, ELEMENT_SIZE> raw;
        std::atomic<size_t> seq{0};
    };

    std::array<Slot, Capacity> data_;

    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};

    static std::intptr_t diff(const size_t l, const size_t r) {
        return static_cast<std::intptr_t>(l) - static_cast<std::intptr_t>(r);
    }

public:
    mpmc_bounded_queue_imp() {
        for (size_t i = 0; i < Capacity; ++i) data_[i].seq.store(i, std::memory_order_relaxed);
    }

    // Approximate / hint only: both counters advance concurrently.
    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    /** @return true on success, false if the queue is full. */
    bool push(const T& in) {
        size_t pos = tail_.load(std::memory_order_relaxed);
        Slot* s;
        while (true) {
            s = &data_[MASK & pos];
            const size_t seq = s->seq.load(std::memory_order_acquire);
            if (const std::intptr_t d = diff(seq, pos); d == 0) {
            	// If Difference is 0, we are at the expected. Attempt to claim.
                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    break; // Claimed successfully
            } else if (d < 0) {
            	// if seq < pos then the queue is still full
                return false;
            } else {
            	// Another Producer already claimed the slot, try again
                pos = tail_.load(std::memory_order_relaxed);
            }
        }

    	// We have claimed the slot. Copy Data and Publish that slot is claimed
        new (static_cast<void*>(s->raw.data())) T(in);
        s->seq.store(pos + 1, std::memory_order_release);
        return true;
    }

    /**
     * Moves the front element into @p fill and destroys it.
     * @return true on success, false if the queue is empty.
     */
    bool pop(T& fill) {
        size_t pos = head_.load(std::memory_order_relaxed);
        Slot* s;
    	while (true) {
            s = &data_[MASK & pos];
            const size_t seq = s->seq.load(std::memory_order_acquire);
    		// Looking for head + 1
            if (const std::intptr_t d = diff(seq, pos + 1); d == 0) {
            	// If seq = pos + 1 then a producer has finished writing and we can consume the slot
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    break; // We have claimed this slot
            } else if (d < 0) {
                return false; // There is nothing to consume (queue is empty)
            } else {
            	// lapped, retry.
                pos = head_.load(std::memory_order_relaxed);
            }
        }

    	// Copy over the data entirely before publishing we have completed by increating the sequence
        T* p = std::launder(reinterpret_cast<T*>(s->raw.data()));
        fill = std::move(*p);
        p->~T();

    	// Release this slot for producer on next lap
        s->seq.store(pos + Capacity, std::memory_order_release);
        return true;
    }

	/** Non Thread Safe Reset and Tear down */
    void clear() {
        size_t h = head_.load(std::memory_order_relaxed);
        const size_t t = tail_.load(std::memory_order_relaxed);
        while (h != t) {
        	Slot& s = data_[MASK & h];
            T* p = std::launder(reinterpret_cast<T*>(s.raw.data()));
            p->~T();
            s.seq.store(h + Capacity, std::memory_order_relaxed);
            ++h;
        }
        head_.store(h, std::memory_order_relaxed);
    }

    ~mpmc_bounded_queue_imp() { clear(); }
};
/**
 * @Lock Free Multi Producer Multi Consumer Ring Buffer
 *
 * @tparam T                  Element type
 * @tparam RequestedCapacity  Minimum number of elements the queue can hold
*/
 template<typename T, size_t RequestedCapacity>
 class mpmc_bounded_queue : public mpmc_bounded_queue_imp<T, dsl::round_up_pow2(RequestedCapacity)> {
 	public:
 	static constexpr size_t capacity = dsl::round_up_pow2(RequestedCapacity);
};

}
#endif  // DSL_DCLC_MPMC_BOUNDED_QUEUE_H_
