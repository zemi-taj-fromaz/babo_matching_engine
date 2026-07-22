//
// Created by Adminstudio on 6/26/2026.
//

#ifndef BABOMATCHINGENGINE_PIN_H
#define BABOMATCHINGENGINE_PIN_H

#include <array>
#include <cassert>
#include <cstdint>
#include <utility>

#include "../book/simple_order.h"
#include "../memory/memory_pool.h"

namespace babo {

// Fixed compile-time capacity of a PIN node (orders per node) is 64.
// Override at build time with -DBABO_PIN_CAPACITY=N (e.g. 16/32/64/128)
// larger nodes pack more orders per cache-contiguous block (fewer node allocations / chain hops)
// at the cost of a bigger, sparser node when levels are thin. Default 64.
#ifndef BABO_PIN_CAPACITY
#define BABO_PIN_CAPACITY 64
#endif
inline constexpr std::uint16_t kNodeCapacity = BABO_PIN_CAPACITY;

// nodes are links in a global chain
// next/prev is an intrusive pointer list inside of a node, reordering is cause by cancelling//filling//adding orders
template <std::uint16_t N>
class pin_node {
	static_assert(N > 0, "pin_node capacity must be positive");
public:
	static constexpr std::uint16_t npos = static_cast<std::uint16_t>(-1);
	static_assert(N < npos, "pin_node capacity out of range");

	pin_node()
	{
		// Thread the free list through links_[].next.
		for (std::uint16_t i = 0; i < N; ++i) links_[i].next = static_cast<std::uint16_t>(i + 1);
		links_[N - 1].next = npos;
	}

	[[nodiscard]] bool empty() const noexcept { return size_ == 0; }
	[[nodiscard]] bool full()  const noexcept { return size_ == N; }
	[[nodiscard]] std::uint16_t size() const noexcept { return size_; }
	[[nodiscard]] static constexpr std::uint16_t capacity() noexcept { return N; }

	[[nodiscard]] std::uint16_t head() const noexcept { return head_; }
	[[nodiscard]] std::uint16_t tail() const noexcept { return tail_; }
	[[nodiscard]] std::uint16_t next(std::uint16_t s) const noexcept { return links_[s].next; }
	[[nodiscard]] std::uint16_t prev(std::uint16_t s) const noexcept { return links_[s].prev; }

	[[nodiscard]] simple::SimpleOrder&       at(std::uint16_t s)       noexcept { return slots_[s]; }
	[[nodiscard]] const simple::SimpleOrder& at(std::uint16_t s) const noexcept { return slots_[s]; }

	[[nodiscard]] pin_node* next_node() const noexcept { return next_node_; }
	[[nodiscard]] pin_node* prev_node() const noexcept { return prev_node_; }
	void set_next_node(pin_node* n) noexcept { next_node_ = n; }
	void set_prev_node(pin_node* n) noexcept { prev_node_ = n; }

	std::uint16_t append(simple::SimpleOrder&& order)
	{
		const std::uint16_t slot = alloc(std::move(order));
		link_back(slot);
		return slot;
	}

	std::uint16_t prepend(simple::SimpleOrder&& order)
	{
		const std::uint16_t slot = alloc(std::move(order));
		link_front(slot);
		return slot;
	}

	std::uint16_t insert_after(std::uint16_t reference, simple::SimpleOrder&& order)
	{
		const std::uint16_t slot = alloc(std::move(order));
		splice_after(reference, slot);
		return slot;
	}

	void erase(std::uint16_t s) noexcept
	{
		const std::uint16_t p = links_[s].prev, n = links_[s].next;
		if (p == npos) head_ = n; else links_[p].next = n;
		if (n == npos) tail_ = p; else links_[n].prev = p;
		free_slot(s);
	}

private:
	std::uint16_t alloc(simple::SimpleOrder&& order)
	{
		assert(!full());
		const std::uint16_t slot = free_;
		free_ = links_[slot].next;
		slots_[slot] = std::move(order);
		++size_;
		return slot;
	}

	void free_slot(std::uint16_t s) noexcept
	{
		links_[s].next = free_;
		free_ = s;
		--size_;
	}

	void link_front(std::uint16_t s) noexcept
	{
		links_[s].prev = npos;
		links_[s].next = head_;
		if (head_ == npos) tail_ = s; else links_[head_].prev = s;
		head_ = s;
	}

	void link_back(std::uint16_t s) noexcept
	{
		links_[s].next = npos;
		links_[s].prev = tail_;
		if (tail_ == npos) head_ = s; else links_[tail_].next = s;
		tail_ = s;
	}

	void splice_after(std::uint16_t ref, std::uint16_t s) noexcept
	{
		const std::uint16_t n = links_[ref].next;
		links_[s].prev = ref;
		links_[s].next = n;
		links_[ref].next = s;
		if (n == npos) tail_ = s; else links_[n].prev = s;
	}

	struct link {
		std::uint16_t prev{npos};
		std::uint16_t next{npos};
	};

	pin_node* next_node_ = nullptr;   // toward the tail (worse priority) of the global chain
	pin_node* prev_node_ = nullptr;   // toward the head (better priority)

	std::array<simple::SimpleOrder, N> slots_;
	std::array<link, N> links_;
	std::uint16_t head_ = npos;
	std::uint16_t tail_ = npos;
	std::uint16_t free_ = 0;
	std::uint16_t size_ = 0;
};

using pin_node_t = pin_node<kNodeCapacity>;

// Process-wide unsynchronized pool for pin_node_t objects. All books in this
// process/module must be constructed, operated, and destroyed on one matching
// thread and must not have static-storage duration.
inline memory::AllocatorPool<pin_node_t, 128>& pin_node_pool()
{
	static memory::AllocatorPool<pin_node_t, 128> pool;
	return pool;
}

} // namespace babo

#endif // BABOMATCHINGENGINE_PIN_H
