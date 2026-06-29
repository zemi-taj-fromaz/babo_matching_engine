//
// Created by hrcol on 29.6.2026..
//

#ifndef BABOMATCHINGENGINE_NARB_TREE_H
#define BABOMATCHINGENGINE_NARB_TREE_H

#include "price_level_descriptor.h"

namespace babo
{
#include <cstdint>

enum class order_type { BID, ASK };

struct search_result
{
    price_level_descriptor *existing, *pred, *succ;
};

template <order_type type>
class narb_tree
{
public:
    price_level_descriptor* get_best();

    search_result find_neighbors(std::uint64_t price);
    void neighbor_aware_insert(price_level_descriptor *new_node, price_level_descriptor *pred, price_level_descriptor *succ);

    /*
     *iterator metode itd
     */
private:
    price_level_descriptor* _best;
    price_level_descriptor* _root;

    std::uint32_t _node_capacity{};

    void insert_rebalance(price_level_descriptor *new_node);

    void right_rotate(price_level_descriptor* node);
    void left_rotate(price_level_descriptor* node);
};

template <order_type type>
price_level_descriptor* narb_tree<type>::get_best()
{
    return _best;
}

template <order_type type>
search_result narb_tree<type>::find_neighbors(std::uint64_t price)
{
    if (!_root)  return {nullptr, nullptr, nullptr};
    price_level_descriptor* curr = _root;
    while (curr)
    {
        if (price < curr->_price)
        {
            if (!curr->left) return {nullptr, curr->pred, curr};
            curr = curr->left;
        }
        else if (price > curr->_price)
        {
            if (!curr->right) return {nullptr, curr, curr->succ};
            curr = curr->right;
        }
        else return {curr, curr->pred, curr->succ};
    }

    return {nullptr, nullptr, nullptr};
}

template <order_type type>
void narb_tree<type>::insert_rebalance(price_level_descriptor *x)
{
    x->_color = color::RED;

    while ( (x != _root) && (x->parent->_color == color::RED) )
    {
       if ( x->parent == x->parent->parent->left )
       {
           /* If x's parent is a left, y is x's right 'uncle' */
            price_level_descriptor* y = x->parent->parent->right;
           if ( y && y->_color == color::RED){
               x->parent->_color = color::BLACK;
               y->_color = color::BLACK;
               x->parent->parent->_color = color::RED;
               x = x->parent->parent;
               }
           else {
               if ( x == x->parent->right ) {
                   x = x->parent;
                   left_rotate(x);
               }

               x->parent->_color = color::BLACK;
               x->parent->parent->_color = color::RED;
               right_rotate(x->parent->parent );
           }
       }
       else {
            // Symmetric case for when parent is a right child
            price_level_descriptor* y = x->parent->parent->left; // Uncle

            if (y && y->_color == color::RED) {
                x->parent->_color = color::BLACK;
                y->_color = color::BLACK;
                x->parent->parent->_color = color::RED;
                x = x->parent->parent;
            } else {
                if (x == x->parent->left) {
                    x = x->parent;
                    right_rotate(x);
                }
                x->parent->_color = color::BLACK;
                x->parent->parent->_color = color::RED;
                left_rotate(x->parent->parent);
            }
       }
    }
    /* Colour the root black */
    _root->_color = color::BLACK;
}


template <order_type type>
void narb_tree<type>::neighbor_aware_insert(price_level_descriptor *new_node, price_level_descriptor *pred, price_level_descriptor *succ)
{
    //check for emty tree

    if (!_root)
    {
        _root = new_node;
        _best = new_node;
    }
    else
    {
        if (pred && !pred->right)
        {
            pred->right = new_node;
            new_node->parent = pred;
        } else
        {
            succ->left = new_node;
            new_node->parent = succ;
        }
    }

    new_node->pred = pred;
    new_node->succ = succ;
    if (pred) pred->succ = new_node;
    if (succ) succ->pred = new_node;

   // 4. Update _best pointer if needed (O(1) price discovery)
    // For ASKs: Best is MIN (no predecessor)
    // For BIDs: Best is MAX (no successor)
    if constexpr(type == order_type::BID)
    {
        if (!succ) _best = new_node;
    }
    else if constexpr(type == order_type::ASK)
    {
        if (!pred) _best = new_node;
    }

    //update best pointer if needed
    //update neighbor links and pin chain
    //insertion fixup
    this->insert_rebalance(new_node);
}

}

#endif //BABOMATCHINGENGINE_NARB_TREE_H
