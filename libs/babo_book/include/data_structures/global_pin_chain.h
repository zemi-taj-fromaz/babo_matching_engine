//
// Created by hrcol on 26.6.2026..
//

#ifndef BABOMATCHINGENGINE_GLOBAL_PIN_CHAIN_H
#define BABOMATCHINGENGINE_GLOBAL_PIN_CHAIN_H

#include "../book/types.h"
#include "pin_node.h"

namespace babo
{
template <class T>
struct global_pin_chain
{
    pin_node<T>* head;
    pin_node<T>* tail;
};
}

#endif //BABOMATCHINGENGINE_GLOBAL_PIN_CHAIN_H
