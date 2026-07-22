//
// Created by hrcol on 26.6.2026..
//

#ifndef BABOMATCHINGENGINE_PRICE_LEVEL_DESCRIPTOR_H
#define BABOMATCHINGENGINE_PRICE_LEVEL_DESCRIPTOR_H

#include "pin_node.h"

#include <cstdint>

namespace babo  {

struct order_loc
{
    pin_node_t* pin_loc;   // the concrete pin_node
    uint16_t index;        // slot index within pin_loc

    [[nodiscard]] bool valid() const noexcept { return pin_loc != nullptr; }
    bool operator==(const order_loc&) const = default;
};

enum class color : uint8_t {RED, BLACK};

// Metadata for a price level. It does not own pin_nodes -- orders live in the narb_trees global pin chain
struct price_level_descriptor
{
    price_level_descriptor *left, *right, *parent;
    price_level_descriptor *pred, *succ;

    color _color;
    uint64_t _price;
    uint32_t _quantity;   // aggregate open qty resting at this level
    uint32_t _count;      // number of resting orders at this level (for O(SIZE) depth walk)

    order_loc _head;   // highest-priority (best) order at this level
    order_loc _tail;   // lowest-priority (newest) order at this level

    [[nodiscard]] bool empty() const noexcept { return _head.pin_loc == nullptr; }
};

// Process-wide unsynchronized pool for price_level_descriptor objects. It has
// the same one-matching-thread and non-static-book lifetime contract as
// pin_node_pool(). allocate() value-initialises the descriptor.
inline memory::AllocatorPool<price_level_descriptor>& price_level_descriptor_pool()
{
    static memory::AllocatorPool<price_level_descriptor> pool;
    return pool;
}

}
#endif //BABOMATCHINGENGINE_PRICE_LEVEL_DESCRIPTOR_H
