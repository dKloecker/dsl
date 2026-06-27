#ifndef DSL_POOL_ALLOCATOR_H
#define DSL_POOL_ALLOCATOR_H

#include <algorithm>
#include <cstddef>
#include <list>
#include <memory>
#include <stdexcept>

#include "dsl/core/concepts/dsl_concepts.h"
#include "dsl/core/utils/dsl_util.h"

namespace dsl {
/**
 * @brief Fixed-size pool allocator backed by over-allocated aligned memory blocks.
 *
 * Pre-allocates blocks of memory divided into fixed-size chunks.
 * Blocks grow on demand and are never moved, so pointers into the pool
 * remain stable for the allocator's lifetime, after which the resource will be returned
 * to its upstream.
 *
 * Configured through construction via @c pool_options struct
 * The final chunk size may be larger than requested, depending on the alignment.
 * But all chunks are guaranteed to be compatible with the requested alignment.
 */
class fixed_size_pool_resource : public std::pmr::memory_resource {
	struct Block;
public:
	struct pool_options {
		size_t chunk_size;
		size_t chunks_per_block;
		size_t alignment = alignof(std::max_align_t);
	};

	/**
	 * @param opts Options for the desired pool configuration
	 * @param upstream upstream memory resource from which memory is obtained.
	 * @throws std::invalid_argument if invalid chunks_per_block
	 * @throws std::invalid_argument if invalid alignment provided
	 */
		explicit fixed_size_pool_resource(
			const pool_options &opts,
			std::pmr::memory_resource* upstream = std::pmr::get_default_resource()
		)
			: options_{
				.chunk_size       = align_up(std::max(opts.chunk_size, sizeof(void *)), std::max(alignof(Block), opts.alignment)),
				.chunks_per_block = opts.chunks_per_block,
				.alignment        = std::max(alignof(Block), opts.alignment)
			}
			, upstream_(upstream) {
			if (opts.chunks_per_block == 0) throw std::invalid_argument("chunks_per_block must be greater than zero");
			if (opts.alignment == 0) throw std::invalid_argument("alignment must be greater than zero");
			allocate_new_block();
		}

	fixed_size_pool_resource(fixed_size_pool_resource && other) noexcept
		: options_(other.options_)
		, upstream_(std::exchange(other.upstream_, nullptr))
		, block_head_(std::exchange(other.block_head_, nullptr))
		, free_(std::exchange(other.free_, nullptr))
		{
		}


	fixed_size_pool_resource(const fixed_size_pool_resource &) = delete;

	fixed_size_pool_resource &operator=(const fixed_size_pool_resource &) = delete;

	~fixed_size_pool_resource() override {
		Block* block = block_head_;
		while (block != nullptr) {
			Block* next = block->next;
			// End Block life time, and free from upstream resource.
			block->~Block();
			upstream_->deallocate(block, total_block_size(), options_.alignment);
			block = next;
		}
		free_ = nullptr; // Reset to nullptr, as it belonged to one of the de-allocated block
	}

private:
	/** Options Provided to Pool Resource must remain fixed once constructed */
	const pool_options options_;

	/**
	 * A free chunk. Occupies `chunk_size` bytes in the backing Block.
	 * When free, the first bytes hold a pointer to the next free chunk.
	 * When allocated, the caller owns those bytes.
	 */
	struct Chunk {
		Chunk *next = nullptr;
	};

	std::pmr::memory_resource *upstream_;


	/**
	 * Total bytes needed for a single over-allocated Block:
	 * the Block header (rounded up to alignment) plus all chunk data.
	 */
	[[nodiscard]] size_t total_block_size() const {

		const size_t header = align_up(sizeof(Block), options_.alignment);
		return header + (options_.chunks_per_block * options_.chunk_size);
	}

	/**
	 * A contiguous block of memory divided into a fixed number of chunks.
	 * Blocks are over-allocated with data living in trailing bytes after
	 * the block header.
	 * Blocks form a linked list via @c next for ownership tracking.
	 * @code
	 *  <----------------- total_block_size() ------------------>
	 *  <-- header (aligned) ---> <----- chunk data region ----->
	 * [ Block struct | padding ] [ chunk 0 | chunk 1 |   ...   ]
	 * @endcode
	 */
	struct Block {
		Block *	next;
		const std::size_t block_alignment;

		/**
		 * Returns a pointer to the start of the aligned chunk data region.
		 */
		std::byte *data() {
			// Address past header
			const auto addr = reinterpret_cast<uintptr_t>(this) + sizeof(Block);
			// Address past aligned header (including padding)
			return reinterpret_cast<std::byte *>(align_up(addr, block_alignment));
		}
	};

	/** Owns all allocated blocks. New blocks are prepended here. */
	Block* block_head_ = nullptr;

	/** Head of the free list. The next allocation is served from here. */
	Chunk *free_ = nullptr;

	/**
	 * Allocates a new block, links its chunks into the free list,
	 * and prepends the block to the ownership chain.
	 *
	 * @throws std::bad_alloc on failed allocation of new Block
	 */
	void allocate_new_block() {
		// Retrieve aligned data from upstream resource
		void* raw = upstream_->allocate(
			total_block_size(),
			options_.alignment);

		auto* block  = ::new (raw) Block{.next = nullptr, .block_alignment = options_.alignment};

		auto * head  = reinterpret_cast<Chunk *>(block->data());
		Chunk *tail  = head;

		for (size_t c = 0; c < options_.chunks_per_block - 1; c++) {
			tail->next = reinterpret_cast<Chunk *>(reinterpret_cast<std::byte *>(tail) + options_.chunk_size);
			tail       = tail->next;
		}
		tail->next = nullptr;

		// Link to previous head and make new head of allocated blocklist
		block->next = block_head_;
		block_head_ = block;
		free_       = head;
	}

	/**
	 * Allocate a chunk of memory available for consumption.
	 * Will ignore any provided alignment, as all chunks are aligned to
	 * the configured alignment.
	 * @throws std::bad_alloc when failing to grow if required or incompatible size.
	 */
	[[nodiscard]] void *do_allocate(const size_t bytes, size_t alignment) override {
		if (bytes > options_.chunk_size) throw std::bad_alloc{};
		// TODO: Debug assert? if (alignment < options_.alignment) throw std::bad_alloc{};
		if (!free_) allocate_new_block();

		Chunk *chunk = free_;
		free_        = free_->next;
		return chunk;
	}

	/**
	 * Returns a previously allocated chunk to the free list.
	 * The chunk becomes the new free list head and will be reused first.
	 * Passing a pointer not obtained from this allocator is undefined behavior.
	 */
	void do_deallocate(void *ptr, size_t, size_t) override {
		if (!ptr) return;
		auto *chunk = static_cast<Chunk *>(ptr);
		chunk->next = free_;
		free_       = chunk;
	}

	[[nodiscard]] bool do_is_equal(const memory_resource &other) const noexcept override {
		return this == &other;
	}
};

/**
 * @brief Fixed-size pool allocator with compile time configuration.
 *
 * Thin wrapper over pool_resource_base that feeds template constants as runtime options.
 *
 * @tparam ChunkSize      Minimum required size per chunk
 * @tparam ChunksPerBlock  Number of chunks to allocate within a single block
 * @tparam Alignment       Desired alignment of allocated chunks
 *
 * The final chunk size may be larger than requested, depending on the alignment
 * and size. But all chunks are guaranteed to be compatible with the
 * requested alignment.
 */
template<size_t ChunkSize, size_t ChunksPerBlock, size_t Alignment = alignof(std::max_align_t)> requires (
	power_of_two<Alignment> && ChunksPerBlock > 0)
class static_fixed_size_pool_resource : public fixed_size_pool_resource {
	static constexpr auto make_options() {
		return pool_options{
			.chunk_size       = align_up(std::max(ChunkSize, sizeof(void *)), Alignment),
			.chunks_per_block = ChunksPerBlock,
			.alignment        = Alignment
		};
	}

public:
	static constexpr pool_options options = make_options();

	explicit static_fixed_size_pool_resource(
		std::pmr::memory_resource* upstream = std::pmr::new_delete_resource()) : fixed_size_pool_resource(options, upstream) {}
};
}

#endif //DSL_POOL_ALLOCATOR_H
