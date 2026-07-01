//
// Created by Adminstudio on 6/26/2026.
//

#ifndef BABOMATCHINGENGINE_PIN_H
#define BABOMATCHINGENGINE_PIN_H

#include <cstdint>
#include <vector>

#include "../simple/simple_order.h"

namespace babo {

struct price_level_descriptor;

class pin_node {
public:
	pin_node(price_level_descriptor* pld, std::uint16_t capacity) : _pld(pld), cap_(capacity)
	{
		slots_.reserve(capacity);
		links_.reserve(capacity);
	}
	static constexpr std::uint16_t npos = static_cast<std::uint16_t>(-1);

	// Methods to be added as needed.

private:
	price_level_descriptor* _pld;
	struct link {
		std::uint16_t prev{npos};
		std::uint16_t next{npos};
	};

	// Inter-node chain links (used by the chain layer). Named *_node to avoid
	// colliding with the next()/prev() slot-navigation methods to come.
	pin_node* next_node = nullptr;
	pin_node* prev_node = nullptr;

	// Orders stored by value for cache locality -- the contiguous slot region IS the payload.
	std::vector<simple::SimpleOrder> slots_;  // contiguous payload region (base + stride; never moved)
	std::vector<link> links_;          // per-slot priority indicator (intrusive order links)
	std::uint16_t cap_;                    // capacity C(d); set once in ctor, never changes
	std::uint16_t head_ = npos;
	std::uint16_t tail_ = npos;
	std::uint16_t free_ = 0;               // free-list head, threaded through links_[].next
	std::uint16_t size_ = 0;
};

} // namespace babo

#endif // BABOMATCHINGENGINE_PIN_H
