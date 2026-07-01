//
// Created by hrcol on 26.6.2026..
//

#ifndef BABOMATCHINGENGINE_PRICE_LEVEL_DESCRIPTOR_H
#define BABOMATCHINGENGINE_PRICE_LEVEL_DESCRIPTOR_H

#include "../book/types.h"
#include "pin_node.h"

namespace babo  {

struct order_loc
{
    pin_node* pin_loc;
    uint16_t index;   // slot index within pin_loc; matches pin_node's uint16_t slot addressing
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
};

}
#endif //BABOMATCHINGENGINE_PRICE_LEVEL_DESCRIPTOR_H
