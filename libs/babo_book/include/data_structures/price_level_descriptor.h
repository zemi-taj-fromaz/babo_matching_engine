//
// Created by hrcol on 26.6.2026..
//

#ifndef BABOMATCHINGENGINE_PRICE_LEVEL_DESCRIPTOR_H
#define BABOMATCHINGENGINE_PRICE_LEVEL_DESCRIPTOR_H

#include "../book/types.h"
#include "pin_node.h"

namespace babo  {

template <class T>
struct order_loc
{
    pin_node<T>* pin_loc;
    uint32_t index;
};

template <class T>
struct price_level_descriptor
{
    using Depth = std::uint32_t;

    book::Price price;
    book::Quantity quantity;
    Depth depth;
    order_loc<T> head;
    order_loc<T> tail;
};

}
#endif //BABOMATCHINGENGINE_PRICE_LEVEL_DESCRIPTOR_H
