//
// Created by Adminstudio on 6/26/2026.
//

#ifndef BABOMATCHINGENGINE_PIN_H
#define BABOMATCHINGENGINE_PIN_H

#include <array>
#include <cassert>
#include <cstdint>
#include <utility>

#include "../simple/simple_order.h"
#include "../memory/memory_pool.h"

namespace babo {

struct price_level_descriptor;

// Fixed compile-time capacity of a PIN node (orders per node). Single global value for
// now; can become depth-aware C(d) later by templating the levels on their own capacity.
inline constexpr std::uint16_t kNodeCapacity = 64;

// A Priority-Indicated Node (PIN): a fixed-capacity (N) slot region holding up to N orders,
// stored inline (std::array) for cache locality. Orders are ordered by intrusive prev/next
// slot links (head = highest priority, tail = lowest). Slots never move; insert/erase are
// O(1) link writes with no payload movement.
template <std::uint16_t N>
class pin_node {
	static_assert(N > 0, "pin_node capacity must be positive");
public:
	static constexpr std::uint16_t npos = static_cast<std::uint16_t>(-1);
	static_assert(N < npos, "pin_node capacity out of range");

	explicit pin_node(price_level_descriptor* pld) : _pld(pld)
	{
		// Thread the free list through links_[].next.
		for (std::uint16_t i = 0; i < N; ++i) links_[i].next = static_cast<std::uint16_t>(i + 1);
		links_[N - 1].next = npos;
	}

	// --- state ---
	[[nodiscard]] bool empty() const noexcept { return size_ == 0; }
	[[nodiscard]] bool full()  const noexcept { return size_ == N; }
	[[nodiscard]] std::uint16_t size() const noexcept { return size_; }
	[[nodiscard]] static constexpr std::uint16_t capacity() noexcept { return N; }

	// --- priority-order navigation within this node (head = highest priority) ---
	[[nodiscard]] std::uint16_t head() const noexcept { return head_; }
	[[nodiscard]] std::uint16_t tail() const noexcept { return tail_; }
	[[nodiscard]] std::uint16_t next(std::uint16_t s) const noexcept { return links_[s].next; }
	[[nodiscard]] std::uint16_t prev(std::uint16_t s) const noexcept { return links_[s].prev; }

	// --- payload access ---
	[[nodiscard]] simple::SimpleOrder&       at(std::uint16_t s)       noexcept { return slots_[s]; }
	[[nodiscard]] const simple::SimpleOrder& at(std::uint16_t s) const noexcept { return slots_[s]; }

	// --- inter-node chain links (the level's PIN chain) ---
	[[nodiscard]] pin_node* next_node() const noexcept { return next_node_; }
	[[nodiscard]] pin_node* prev_node() const noexcept { return prev_node_; }
	void set_next_node(pin_node* n) noexcept { next_node_ = n; }
	void set_prev_node(pin_node* n) noexcept { prev_node_ = n; }
	[[nodiscard]] price_level_descriptor* level() const noexcept { return _pld; }

	// --- insertion (caller must ensure !full()) ---
	// Forwarding: copies an lvalue into the slot, moves an rvalue.
	template <class U> std::uint16_t append(U&& v)  { std::uint16_t s = alloc(std::forward<U>(v)); link_back(s);  return s; } // lowest priority
	template <class U> std::uint16_t prepend(U&& v) { std::uint16_t s = alloc(std::forward<U>(v)); link_front(s); return s; } // highest priority

	// --- cancel: unlink + free the slot, no payload movement ---
	void erase(std::uint16_t s) noexcept
	{
		const std::uint16_t p = links_[s].prev, n = links_[s].next;
		if (p == npos) head_ = n; else links_[p].next = n;
		if (n == npos) tail_ = p; else links_[n].prev = p;
		free_slot(s);
	}

private:
	template <class U>
	std::uint16_t alloc(U&& v)
	{
		assert(!full());
		const std::uint16_t s = free_;
		free_ = links_[s].next;
		slots_[s] = std::forward<U>(v);   // pin takes ownership: move an rvalue, copy an lvalue
		++size_;
		return s;
	}

	void free_slot(std::uint16_t s) noexcept
	{
		slots_[s] = simple::SimpleOrder{};   // release the payload promptly
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

	struct link {
		std::uint16_t prev{npos};
		std::uint16_t next{npos};
	};

	price_level_descriptor* _pld;   // owning price level (back-pointer)

	pin_node* next_node_ = nullptr;   // toward the tail of the level's chain
	pin_node* prev_node_ = nullptr;   // toward the head

	std::array<simple::SimpleOrder, N> slots_;  // inline payload region (never moved)
	std::array<link, N> links_;                 // per-slot intrusive order links
	std::uint16_t head_ = npos;
	std::uint16_t tail_ = npos;
	std::uint16_t free_ = 0;                     // free-list head, threaded through links_[].next
	std::uint16_t size_ = 0;
};

// The one concrete node type used across the book while capacity is a single global value.
using pin_node_t = pin_node<kNodeCapacity>;

// Process-wide pool for pin_node_t objects. Meyers singleton: constructed on first use
// with thread-safe initialisation. The pool itself is NOT thread-safe for concurrent
// allocate/release -- fine for the single-threaded matching engine.
// Block size 128 (nodes are large now that slots are inline) -- tune as needed.
inline memory::AllocatorPool<pin_node_t, 128>& pin_node_pool()
{
	static memory::AllocatorPool<pin_node_t, 128> pool;
	return pool;
}

} // namespace babo

#endif // BABOMATCHINGENGINE_PIN_H
