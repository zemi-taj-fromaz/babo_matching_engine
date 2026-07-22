#ifndef BABOMATCHINGENGINE_BOOK_SIMPLE_ORDER_H
#define BABOMATCHINGENGINE_BOOK_SIMPLE_ORDER_H

#include <book/types.h>

#include <cstdint>
#include <stdexcept>

namespace babo { struct price_level_descriptor; }

namespace babo::simple {

enum OrderState {
  os_new,
  os_accepted,
  os_complete,
  os_cancelled,
  os_rejected
};

/// @brief A concrete, non-polymorphic order stored by value in the book's PIN slots.
class SimpleOrder {
public:
  /// @brief Produces an empty placeholder order (order_id_ == 0).
  /// Placeholder slots are never reached because their links stay npos.
  SimpleOrder();

  SimpleOrder(bool is_buy,
              std::uint32_t price,
              std::uint32_t qty,
              std::uint32_t stop_price = 0,
              book::OrderConditions conditions = book::OrderCondition::oc_no_conditions);

  /// @brief Test hook: make the next generated order id equal to 1.
  static void reset_id_generator() { last_order_id_ = 0; }

  const OrderState& state() const;
  bool is_limit() const;
  bool is_buy() const;
  std::uint32_t price() const;
  std::uint32_t stop_price() const;
  std::uint32_t order_qty() const;
  std::uint32_t open_qty() const;

  /// @brief Adjust tentative AON reservation and return remaining availability.
  std::uint32_t reserve(std::int32_t reserved);

  bool filled() const;
  void change_qty(std::int32_t delta);
  const std::uint32_t& filled_qty() const;
  const std::uint32_t& filled_cost() const;
  void fill(std::uint32_t fill_qty, std::uint32_t fill_cost, std::uint32_t fill_id);
  book::OrderConditions conditions() const;
  bool all_or_none() const;
  bool immediate_or_cancel() const;
  void accept();
  void cancel();
  void replace(std::uint32_t size_delta, std::uint32_t new_price);
  void modify(std::int32_t size_delta, std::uint64_t new_price);

private:
  OrderState state_;
  bool is_buy_;
  std::uint32_t order_qty_;
  std::uint32_t price_;
  std::uint32_t stop_price_;
  book::OrderConditions conditions_;
  std::uint32_t filled_qty_;
  std::uint32_t filled_cost_;
  std::int32_t reserved_;
  inline static std::uint32_t last_order_id_{0};

public:
  std::uint32_t order_id_;

  // Back-pointer to the owning price level. It travels with the payload when
  // relocated, preserving the O(1) level lookup used by cancellation.
  babo::price_level_descriptor* _level = nullptr;
};

inline SimpleOrder::SimpleOrder()
  : state_(os_new),
    is_buy_(false),
    order_qty_(0),
    price_(0),
    stop_price_(0),
    conditions_(0),
    filled_qty_(0),
    filled_cost_(0),
    reserved_(0),
    order_id_(0)
{
}

inline SimpleOrder::SimpleOrder(bool is_buy,
                                std::uint32_t price,
                                std::uint32_t qty,
                                std::uint32_t stop_price,
                                book::OrderConditions conditions)
  : state_(os_new),
    is_buy_(is_buy),
    order_qty_(qty),
    price_(price),
    stop_price_(stop_price),
    conditions_(conditions),
    filled_qty_(0),
    filled_cost_(0),
    reserved_(0),
    order_id_(++last_order_id_)
{
}

inline const OrderState& SimpleOrder::state() const { return state_; }
inline bool SimpleOrder::is_limit() const { return price() > 0; }
inline bool SimpleOrder::is_buy() const { return is_buy_; }
inline std::uint32_t SimpleOrder::price() const { return price_; }
inline std::uint32_t SimpleOrder::stop_price() const { return stop_price_; }
inline std::uint32_t SimpleOrder::order_qty() const { return order_qty_; }

inline std::uint32_t SimpleOrder::open_qty() const
{
  if (filled_qty_ >= order_qty_) return 0;

  const std::uint32_t remaining = order_qty_ - filled_qty_;
  if (reserved_ > 0 && static_cast<std::uint32_t>(reserved_) < remaining) {
    return remaining - static_cast<std::uint32_t>(reserved_);
  }
  return reserved_ <= 0 ? remaining : 0;
}

inline std::uint32_t SimpleOrder::reserve(std::int32_t reserved)
{
  reserved_ += reserved;
  return open_qty();
}

inline bool SimpleOrder::filled() const { return filled_qty_ >= order_qty_; }

inline void SimpleOrder::change_qty(std::int32_t delta)
{
  if (delta < 0 && static_cast<std::int32_t>(open_qty()) < -delta) {
    throw std::runtime_error("Replace size reduction larger than open quantity");
  }
  order_qty_ += delta;
}

inline const std::uint32_t& SimpleOrder::filled_qty() const { return filled_qty_; }
inline const std::uint32_t& SimpleOrder::filled_cost() const { return filled_cost_; }

inline void SimpleOrder::fill(std::uint32_t fill_qty,
                              std::uint32_t fill_cost,
                              std::uint32_t /*fill_id*/)
{
  filled_qty_ += fill_qty;
  filled_cost_ += fill_cost;
  if (!open_qty()) state_ = os_complete;
}

inline book::OrderConditions SimpleOrder::conditions() const { return conditions_; }

inline bool SimpleOrder::all_or_none() const
{
  return (conditions_ & book::OrderCondition::oc_all_or_none) != 0;
}

inline bool SimpleOrder::immediate_or_cancel() const
{
  return (conditions_ & book::OrderCondition::oc_immediate_or_cancel) != 0;
}

inline void SimpleOrder::accept()
{
  if (state_ == os_new) state_ = os_accepted;
}

inline void SimpleOrder::cancel()
{
  if (state_ != os_complete) state_ = os_cancelled;
}

inline void SimpleOrder::replace(std::uint32_t size_delta, std::uint32_t new_price)
{
  if (state_ == os_accepted) {
    order_qty_ += size_delta;
    price_ = new_price;
  }
}

inline void SimpleOrder::modify(std::int32_t size_delta, std::uint64_t new_price)
{
  order_qty_ = static_cast<std::uint32_t>(
      static_cast<std::int32_t>(order_qty_) + size_delta);
  if (new_price != 0) price_ = static_cast<std::uint32_t>(new_price);
}

} // namespace babo::simple

#endif // BABOMATCHINGENGINE_BOOK_SIMPLE_ORDER_H
