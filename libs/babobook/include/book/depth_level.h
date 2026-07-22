//
// Created by Adminstudio on 6/25/2026.
//
#ifndef BABOMATCHINGENGINE_DEPTH_LEVEL_H
#define BABOMATCHINGENGINE_DEPTH_LEVEL_H

#include "depth_constants.h"

#include <stdexcept>

namespace babo::book {

/// @brief a single level of the limit order book aggregated by price
class DepthLevel {
public:
  DepthLevel();

  DepthLevel& operator=(const DepthLevel& rhs);

  const uint32_t& price() const;
  uint32_t order_count() const;
  uint32_t aggregate_qty() const;
  bool is_excess() const { return is_excess_; }

  void init(uint32_t price, bool is_excess);


  void add_order(uint32_t qty);

  void increase_qty(uint32_t qty);


  void decrease_qty(uint32_t qty);


  void set(uint32_t price,
           uint32_t qty,
           uint32_t order_count);

  bool close_order(uint32_t qty);


private:
  uint32_t price_;
  uint32_t order_count_;
  uint32_t aggregate_qty_;
  bool is_excess_;
};

inline DepthLevel::DepthLevel()
  : price_(INVALID_LEVEL_PRICE),
  order_count_(0),
  aggregate_qty_(0),
  is_excess_(false)
{
}

inline
DepthLevel& DepthLevel::operator=(const DepthLevel& rhs)
{
  price_ = rhs.price_;
  order_count_ = rhs.order_count_;
  aggregate_qty_ = rhs.aggregate_qty_;

  return *this;
}

inline
const uint32_t&
DepthLevel::price() const
{
  return price_;
}

inline
void
DepthLevel::init(uint32_t price, bool is_excess)
{
  price_ = price;
  order_count_ = 0;
  aggregate_qty_ = 0;
  is_excess_ = is_excess;
}

inline
uint32_t
DepthLevel::order_count() const
{
  return order_count_;
}

inline
uint32_t
DepthLevel::aggregate_qty() const
{
  return aggregate_qty_;
}

inline
void
DepthLevel::add_order(uint32_t qty)
{
  // Increment/increase
  ++order_count_;
  aggregate_qty_ += qty;
}

inline
bool
DepthLevel::close_order(uint32_t qty)
{
  bool empty = false;
  // If this is the last order, reset the level
  if (order_count_ == 0) {
    throw std::runtime_error("DepthLevel::close_order "
      "order count too low");
  } else if (order_count_ == 1) {
    order_count_ = 0;
    aggregate_qty_ = 0;
    empty = true;
    // Else, decrement/decrease
  } else {
    --order_count_;
    if (aggregate_qty_ >= qty) {
      aggregate_qty_ -= qty;
    } else {
      throw std::runtime_error("DepthLevel::close_order "
        "level quantity too low");
    }
  }
  return empty;
}

inline
void
DepthLevel::set(uint32_t price,
  uint32_t qty,
  uint32_t order_count)
{
  price_ = price;
  aggregate_qty_ = qty;
  order_count_ = order_count;
  is_excess_ = false;   // snapshot levels come straight from the tree, never "excess"
}

inline
void
DepthLevel::increase_qty(uint32_t qty)
{
  aggregate_qty_ += qty;
}

inline
void
DepthLevel::decrease_qty(uint32_t qty)
{
  aggregate_qty_ -= qty;
}

}

#endif // BABOMATCHINGENGINE_DEPTH_LEVEL_H
