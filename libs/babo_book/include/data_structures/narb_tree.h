//
// Created by hrcol on 29.6.2026..
//

#ifndef BABOMATCHINGENGINE_NARB_TREE_H
#define BABOMATCHINGENGINE_NARB_TREE_H

#include "price_level_descriptor.h"

#include <cstdint>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <unordered_map>

namespace babo
{

enum class order_type { BID, ASK };

struct search_result
{
    price_level_descriptor *existing, *pred, *succ;
};

template <order_type type>
class narb_tree
{
public:
    using value_type = std::pair<price_level_descriptor*, simple::SimpleOrder>;

    price_level_descriptor* get_best();

    search_result find_neighbors(std::uint64_t price);
    void neighbor_aware_insert(price_level_descriptor *new_node, price_level_descriptor *pred, price_level_descriptor *succ);

    // Bidirectional iterator over price levels in best-first order.
    // Templated on constness so iterator and const_iterator share one body.
    // Walks the pred/succ threads; direction depends on side (resolved at compile time).
    template <bool Const>
    class iterator_impl
    {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type        = price_level_descriptor;
        using difference_type   = std::ptrdiff_t;
        using pointer   = std::conditional_t<Const, const price_level_descriptor*, price_level_descriptor*>;
        using reference = std::conditional_t<Const, const price_level_descriptor&, price_level_descriptor&>;

        iterator_impl() noexcept : _node(nullptr) {}
        explicit iterator_impl(pointer n) noexcept : _node(n) {}

        // implicit iterator -> const_iterator conversion (not the reverse).
        // Templated so it is never mistaken for a copy constructor (which would
        // otherwise suppress the implicit copy ctor when Const == false).
        template <bool OtherConst>
            requires (Const && !OtherConst)
        iterator_impl(const iterator_impl<OtherConst>& other) noexcept
            : _node(other._node) {}

        reference operator*()  const noexcept { return *_node; }
        pointer   operator->() const noexcept { return  _node; }

        // ++ moves toward WORSE price (best-first walk):
        // bids high->low via pred, asks low->high via succ.
        iterator_impl& operator++() noexcept
        {
            if constexpr (type == order_type::BID) _node = _node->pred;
            else                                   _node = _node->succ;
            return *this;
        }
        iterator_impl operator++(int) noexcept { iterator_impl t = *this; ++(*this); return t; }

        // -- moves toward BETTER price (the opposite thread).
        iterator_impl& operator--() noexcept
        {
            if constexpr (type == order_type::BID) _node = _node->succ;
            else                                   _node = _node->pred;
            return *this;
        }
        iterator_impl operator--(int) noexcept { iterator_impl t = *this; --(*this); return t; }

        bool operator==(const iterator_impl& o) const noexcept { return _node == o._node; }
        bool operator!=(const iterator_impl& o) const noexcept { return _node != o._node; }

    private:
        template <bool> friend class iterator_impl;  // so the const-conversion ctor can read _node
        pointer _node;
    };

    using iterator       = iterator_impl<false>;
    using const_iterator = iterator_impl<true>;

    // begin() = best price; end() = nullptr sentinel (the worst level's toward-worse
    // thread terminates at nullptr, so walking off the end lands here naturally).
    iterator       begin()        noexcept { return iterator(_best); }
    iterator       end()          noexcept { return iterator(nullptr); }
    const_iterator begin()  const noexcept { return const_iterator(_best); }
    const_iterator end()    const noexcept { return const_iterator(nullptr); }
    const_iterator cbegin() const noexcept { return const_iterator(_best); }
    const_iterator cend()   const noexcept { return const_iterator(nullptr); }


    iterator find();
    const_iterator find() const;

    // Insert an order into the book.
    //  Branch 1 (O(1) fast path): does it land on, or between, best and second-best?
    //  Branch 2 (general): find_neighbors, then place into an existing level or a new one.
    void insert(simple::SimpleOrder& order);

    // O(1) cancel by order id: locate the slot, unlink it from the PIN chain, and
    // clean up an emptied node (and, if the level empties, the level).
    void erase(std::uint32_t order_id);


private:
    // Allocate + initialise a new price level for the given price.
    price_level_descriptor* make_level(std::uint64_t price);
    // Add an order into a level's PIN chain, record it in the order index.
    void place_order(price_level_descriptor* level, simple::SimpleOrder& order);

    // order id -> resting location, for O(1) cancel.
    std::unordered_map<std::uint32_t, order_loc> _order_index;

    price_level_descriptor* _best = nullptr;
    price_level_descriptor* _root = nullptr;

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
void narb_tree<type>::left_rotate(price_level_descriptor* node)
{
    // node's right child takes node's place; node descends to the left.
    price_level_descriptor* pivot = node->right;

    node->right = pivot->left;
    if (pivot->left) pivot->left->parent = node;

    pivot->parent = node->parent;
    if (!node->parent)                     _root = pivot;
    else if (node == node->parent->left)   node->parent->left = pivot;
    else                                   node->parent->right = pivot;

    pivot->left = node;
    node->parent = pivot;
}

template <order_type type>
void narb_tree<type>::right_rotate(price_level_descriptor* node)
{
    // Mirror of left_rotate: node's left child takes node's place.
    price_level_descriptor* pivot = node->left;

    node->left = pivot->right;
    if (pivot->right) pivot->right->parent = node;

    pivot->parent = node->parent;
    if (!node->parent)                     _root = pivot;
    else if (node == node->parent->right)  node->parent->right = pivot;
    else                                   node->parent->left = pivot;

    pivot->right = node;
    node->parent = pivot;
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

template <order_type type>
void narb_tree<type>::insert(simple::SimpleOrder& order)
{
    const std::uint64_t price = order.price();

    // ---- Branch 1: O(1) fast path against the top of book ----
    if (_best)
    {
        // (a) same price as the best level -> just add the order to it.
        if (price == _best->_price)
        {
            place_order(_best, order);
            return;
        }

        // (a2) more aggressive than best -> new top of book (O(1)).
        // bids: better = higher price ; asks: better = lower price.
        bool better_than_best;
        if constexpr (type == order_type::BID) better_than_best = price > _best->_price;
        else                                   better_than_best = price < _best->_price;
        if (better_than_best)
        {
            price_level_descriptor* level = make_level(price);
            place_order(level, order);
            // The new level sits immediately beyond the old best on the aggressive side,
            // so the old best is its only neighbour (numerically: bids -> old best is the
            // lower/pred side, asks -> the higher/succ side). neighbor_aware_insert then
            // promotes it to _best (it has no succ for bids / no pred for asks).
            if constexpr (type == order_type::BID) neighbor_aware_insert(level, /*pred=*/_best, /*succ=*/nullptr);
            else                                   neighbor_aware_insert(level, /*pred=*/nullptr, /*succ=*/_best);
            return;
        }

        // The second-best level is one step toward worse price.
        // bids: worse = lower price = pred ; asks: worse = higher price = succ.
        price_level_descriptor* next =
            (type == order_type::BID) ? _best->pred : _best->succ;

        // (a3) best has no worse neighbour -> it is the ONLY level, and (having passed
        // (a)/(a2)) price is worse than best. Splice the new level straight onto best's
        // worse side. O(1), no traversal. Mirrors find_neighbors' single-node result.
        if (!next)
        {
            price_level_descriptor* level = make_level(price);
            place_order(level, order);
            if constexpr (type == order_type::BID) neighbor_aware_insert(level, /*pred=*/nullptr, /*succ=*/_best);
            else                                   neighbor_aware_insert(level, /*pred=*/_best,    /*succ=*/nullptr);
            return;
        }

        // (b) same price as the second-best level.
        if (price == next->_price)
        {
            place_order(next, order);
            return;
        }

        // (c) strictly between best and second-best -> new level in the gap.
        // find_neighbors/neighbor_aware_insert use raw price (pred = lower price,
        // succ = higher price), so just pass the numeric lower/higher of the two.
        price_level_descriptor* lo = (_best->_price < next->_price) ? _best : next;
        price_level_descriptor* hi = (_best->_price < next->_price) ? next : _best;
        if (price > lo->_price && price < hi->_price)
        {
            price_level_descriptor* level = make_level(price);
            place_order(level, order);
            neighbor_aware_insert(level, /*pred=*/lo, /*succ=*/hi);
            return;
        }
    }

    // ---- Branch 2: general path ----
    search_result sr = find_neighbors(price);
    if (sr.existing)
    {
        // Price already has a level: drop the order into it.
        place_order(sr.existing, order);
    }
    else
    {
        // New price: create a level, seed it with the order, splice it into the tree.
        price_level_descriptor* level = make_level(price);
        place_order(level, order);
        neighbor_aware_insert(level, sr.pred, sr.succ);
    }
}

template <order_type type>
price_level_descriptor* narb_tree<type>::make_level(std::uint64_t price)
{
    // TODO: allocate from a pool instead of `new` (hot path -- avoid per-level heap churn).
    price_level_descriptor* level = new price_level_descriptor{};
    level->left = level->right = level->parent = nullptr;
    level->pred = level->succ = nullptr;
    level->_color = color::RED;   // neighbor_aware_insert -> insert_rebalance fixes colours
    level->_price = price;
    level->_quantity = 0;
    level->_depth = 0;
    level->_head = {nullptr, 0};  // empty PIN chain; first insert allocates the node
    level->_tail = {nullptr, 0};
    return level;
}

template <order_type type>
void narb_tree<type>::place_order(price_level_descriptor* level, simple::SimpleOrder& order)
{
    // Read the id before insert moves the order into its slot, then record where it landed.
    const std::uint32_t id = order.order_id_;
    const order_loc loc = level->insert(order);   // appends into the level's PIN chain
    _order_index[id] = loc;
}

template <order_type type>
void narb_tree<type>::erase(std::uint32_t order_id)
{
    auto it = _order_index.find(order_id);
    if (it == _order_index.end()) return;   // unknown / already cancelled

    const order_loc loc = it->second;
    price_level_descriptor* level = loc.pin_loc->level();   // owning level (back-pointer)
    _order_index.erase(it);

    const bool level_empty = level->remove(loc);   // unlink slot + rewire PIN chain
    if (level_empty)
    {
        // The price level has no more orders. It must be removed from the tree.
        // TODO: RB-delete `level` (fixup pred/succ threads, _best, and free the descriptor).
        //       Until RB-delete exists, the empty descriptor stays in the tree as a zombie.
    }
}

}

#endif //BABOMATCHINGENGINE_NARB_TREE_H
