//
// Created by Adminstudio on 6/26/2026.
//

#ifndef BABOMATCHINGENGINE_PIN_H
#define BABOMATCHINGENGINE_PIN_H

#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

namespace babo {

// A Priority-Indicated Node (PIN): a fixed-capacity priority-queue node that stores up to C
// entries in a contiguously addressable slot region (base + stride; slots are never moved or
// compacted). Each slot's "priority indicator" is realized here as intrusive order links
// (prev/next slot indices) plus head/tail anchors, so the node resolves head/tail and any
// insertion position in O(1) by link writes alone -- no entry comparisons, and no payload
// movement on insert or erase (the property that makes the 95%-cancel path cheap).
//
// This is the single node only. Cross-node relocation cascades (Push Back/Forward, H_max) and
// depth-aware capacity C(d) live in the chain layer built on top of this.
template <class T>
class pin_node {
public:
	using slot_type = std::uint16_t;
	static constexpr slot_type npos = static_cast<slot_type>(-1);

	// Capacity C is a runtime value (depth-aware: C(d)), fixed for this node's lifetime.
	// Sized once here; slots are never reallocated, so payload addresses stay stable.
	explicit pin_node(slot_type capacity)
		: slots_(capacity), links_(capacity), cap_(capacity) {
		assert(capacity > 0 && capacity < npos && "capacity out of range");
		// Thread the free list through links_[].next.
		for (slot_type i = 0; i < cap_; ++i) links_[i].next = static_cast<slot_type>(i + 1);
		links_[cap_ - 1].next = npos;
	}

	[[nodiscard]] bool empty() const noexcept { return size_ == 0; }
	[[nodiscard]] bool full() const noexcept { return size_ == cap_; }
	[[nodiscard]] slot_type size() const noexcept { return size_; }
	[[nodiscard]] slot_type capacity() const noexcept { return cap_; }

	// Priority-order navigation: head = highest priority, tail = lowest. next() walks toward the
	// tail (decreasing priority), prev() toward the head. npos terminates in either direction.
	[[nodiscard]] slot_type head() const noexcept { return head_; }
	[[nodiscard]] slot_type tail() const noexcept { return tail_; }
	[[nodiscard]] slot_type next(slot_type s) const noexcept { return links_[s].next; }
	[[nodiscard]] slot_type prev(slot_type s) const noexcept { return links_[s].prev; }

	[[nodiscard]] T& at(slot_type s) noexcept { return slots_[s]; }
	[[nodiscard]] const T& at(slot_type s) const noexcept { return slots_[s]; }

	// Becomes the highest-priority entry in the node.
	slot_type prepend(const T& v) { slot_type s = alloc(v); link_front(s); return s; }
	slot_type prepend(T&& v) { slot_type s = alloc(std::move(v)); link_front(s); return s; }

	// Becomes the lowest-priority entry in the node.
	slot_type append(const T& v) { slot_type s = alloc(v); link_back(s); return s; }
	slot_type append(T&& v) { slot_type s = alloc(std::move(v)); link_back(s); return s; }

	// Insert immediately lower priority than ref (right after ref, toward the tail).
	slot_type insert_after(slot_type ref, const T& v) { return splice_after(ref, alloc(v)); }
	slot_type insert_after(slot_type ref, T&& v) { return splice_after(ref, alloc(std::move(v))); }

	// Insert immediately higher priority than ref (right before ref, toward the head).
	slot_type insert_before(slot_type ref, const T& v) { return ref == head_ ? prepend(v) : splice_after(links_[ref].prev, alloc(v)); }
	slot_type insert_before(slot_type ref, T&& v) { return ref == head_ ? prepend(std::move(v)) : splice_after(links_[ref].prev, alloc(std::move(v))); }

	// Cancel: unlink and free the slot. No payload movement; other slots keep their addresses.
	void erase(slot_type s) noexcept {
		const slot_type p = links_[s].prev, n = links_[s].next;
		if (p == npos) head_ = n; else links_[p].next = n;
		if (n == npos) tail_ = p; else links_[n].prev = p;
		free_slot(s);
	}

private:
	struct link {
		slot_type prev{npos};
		slot_type next{npos};
	};

	template <class U>
	slot_type alloc(U&& v) {
		assert(!full());
		const slot_type s = free_;
		free_ = links_[s].next;
		slots_[s] = std::forward<U>(v);
		++size_;
		return s;
	}

	void free_slot(slot_type s) noexcept {
		slots_[s] = T{}; // release any owned resources (e.g. shared_ptr) promptly
		links_[s].next = free_;
		free_ = s;
		--size_;
	}

	slot_type splice_after(slot_type ref, slot_type s) noexcept {
		if (ref == tail_) {
			links_[s].prev = tail_;
			links_[s].next = npos;
			if (tail_ == npos) head_ = s; else links_[tail_].next = s;
			tail_ = s;
			return s;
		}
		const slot_type n = links_[ref].next;
		links_[s].prev = ref;
		links_[s].next = n;
		links_[ref].next = s;
		links_[n].prev = s;
		return s;
	}

	void link_front(slot_type s) noexcept {
		links_[s].prev = npos;
		links_[s].next = head_;
		if (head_ == npos) tail_ = s; else links_[head_].prev = s;
		head_ = s;
	}

	void link_back(slot_type s) noexcept {
		links_[s].next = npos;
		links_[s].prev = tail_;
		if (tail_ == npos) head_ = s; else links_[tail_].next = s;
		tail_ = s;
	}

	// Inter-node chain links (used by the chain layer). Named *_node to avoid
	// colliding with the next()/prev() slot-navigation methods above.
	pin_node* next_node = nullptr;
	pin_node* prev_node = nullptr;

	std::vector<T> slots_;        // contiguous payload region (base + stride; never moved)
	std::vector<link> links_;     // per-slot priority indicator (intrusive order links)
	slot_type cap_;               // capacity C(d); set once in ctor, never changes
	slot_type head_ = npos;
	slot_type tail_ = npos;
	slot_type free_ = 0;          // free-list head, threaded through links_[].next
	slot_type size_ = 0;
};

} // namespace babo

#endif // BABOMATCHINGENGINE_PIN_H
