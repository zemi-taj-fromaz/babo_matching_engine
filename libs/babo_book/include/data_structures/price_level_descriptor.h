//
// Created by hrcol on 26.6.2026..
//

#ifndef BABOMATCHINGENGINE_PRICE_LEVEL_DESCRIPTOR_H
#define BABOMATCHINGENGINE_PRICE_LEVEL_DESCRIPTOR_H

#include "../book/types.h"
#include "pin_node.h"

#include <utility>

namespace babo  {

struct order_loc
{
    pin_node_t* pin_loc;   // the one concrete pin_node type (pin_node<kNodeCapacity>)
    uint16_t index;        // slot index within pin_loc; matches pin_node's uint16_t addressing
};

enum class color : uint8_t {RED, BLACK};

struct price_level_descriptor
{

    price_level_descriptor *left, *right, *parent;
    price_level_descriptor *pred, *succ;

    color _color;
    uint64_t _price;
    uint32_t _quantity;
    uint32_t _depth;

    order_loc _head;
    order_loc _tail;

    // Insert an order at this price level. Strict price-time: the new order is the
    // lowest-priority order at the level, so it is appended at the tail node.
    // Returns the slot location where the order came to rest.
    order_loc insert(simple::SimpleOrder& order)
    {
        // Read the quantity before the order is moved into a slot (moved-from after).
        const uint32_t qty = order.open_qty();
        order_loc loc{};

        if (!_head.pin_loc)
        {
            // First order at this level: allocate the level's first PIN node.
            pin_node_t* node = pin_node_pool().allocate(this);
            uint16_t s = node->append(std::move(order));
            _head = {node, s};
            _tail = {node, s};
            loc = {node, s};
        }
        else
        {
            pin_node_t* tail_node = _tail.pin_loc;
            if (!tail_node->full())
            {
                uint16_t s = tail_node->append(std::move(order));
                _tail = {tail_node, s};
                loc = {tail_node, s};
            }
            else
            {
                // Tail node full.
                // TODO: directed relocation cascade (Push Back/Forward, capped at D_max)
                //       to reuse a free slot in a neighbouring node before allocating.
                // Terminal case (correct, unoptimised): append a fresh node at the tail.
                pin_node_t* node = pin_node_pool().allocate(this);
                node->set_prev_node(tail_node);
                tail_node->set_next_node(node);
                uint16_t s = node->append(std::move(order));
                _tail = {node, s};
                loc = {node, s};
            }
        }
        _quantity += qty;
        return loc;
    }

    // Remove the order at `loc` from this level's PIN chain. Rewires the intrusive
    // links (O(1) inside the node), drops an emptied node from the chain, and keeps
    // _head/_tail pointing at the real head/tail orders.
    // Returns true if the level now holds no orders.
    bool remove(order_loc loc)
    {
        pin_node_t* node = loc.pin_loc;
        _quantity -= node->at(loc.index).open_qty();   // adjust aggregate before freeing slot
        node->erase(loc.index);                        // unlink slot + return it to the free list

        if (node->empty())
        {
            // Drop the now-empty node from the level's PIN chain.
            pin_node_t* prev = node->prev_node();
            pin_node_t* next = node->next_node();
            if (prev) prev->set_next_node(next);
            if (next) next->set_prev_node(prev);

            if (!prev && !next)
            {
                // Was the level's only node -> the level is now empty.
                _head = {nullptr, 0};
                _tail = {nullptr, 0};
                pin_node_pool().release(node);   // recycle the emptied node
                return true;
            }
            if (!prev) _head = {next, next->head()};   // node was the chain head
            if (!next) _tail = {prev, prev->tail()};   // node was the chain tail
            pin_node_pool().release(node);   // recycle the emptied node
            return false;
        }

        // Node still holds orders: if we removed the level's head/tail order,
        // refresh _head/_tail to the node's new head/tail slot.
        if (node == _head.pin_loc) _head = {node, node->head()};
        if (node == _tail.pin_loc) _tail = {node, node->tail()};
        return false;
    }
};

}
#endif //BABOMATCHINGENGINE_PRICE_LEVEL_DESCRIPTOR_H
