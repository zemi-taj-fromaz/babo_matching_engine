#ifndef BABOMATCHINGENGINE_SIMPLE_ORDER_H
#define BABOMATCHINGENGINE_SIMPLE_ORDER_H


#define LIQUIBOOK_ORDER_KNOWS_CONDITIONS
#include <book/order.hpp>
#include <book/types.h>

namespace babo::simple {

enum OrderState {
  os_new,
  os_accepted,
  os_complete,
  os_cancelled,
  os_rejected
};

/// @brief impelementation of the Order interface for testing purposes.
class SimpleOrder : public book::Order {
public:
  SimpleOrder(bool is_buy,
              uint32_t price,
              uint32_t qty,
              uint32_t stop_price = 0,
              book::OrderConditions conditions = book::OrderCondition::oc_no_conditions);

  /// @brief get the order's state
  const OrderState& state() const;

  /// @brief is this order a buy?
  virtual bool is_buy() const;

  /// @brief get the limit price of this order
  virtual uint32_t price() const;

  virtual uint32_t stop_price()const;

  /// @brief get the quantity of this order
  virtual uint32_t order_qty() const;

  /// @brief get the open quantity of this order (remaining, minus any reserved)
  virtual uint32_t open_qty() const;

  /// @brief tentatively set aside (reserve) quantity during AON deferred-match
  /// planning, without filling it. Lowers the reported open_qty() so the same
  /// quantity isn't double-counted while an AON match is being assembled.
  /// @param reserved amount to add to the reservation (may be negative to release)
  /// @return the remaining available quantity after this reservation
  uint32_t reserve(int32_t reserved);

  /// @brief is there no remaining open quantity in this order?
  bool filled() const;

  /// @brief modify the order's quantity by delta (used by replace).
  /// Mirrors the old OrderTracker::change_qty now that the order tracks itself.
  void change_qty(int32_t delta);

  /// @brief get the filled quantity of this order
  virtual const uint32_t& filled_qty() const;

  /// @brief get the total filled cost of this order
  const uint32_t& filled_cost() const;

  /// @brief notify of a fill of this order
  /// @param fill_qty the number of shares in this fill
  /// @param fill_cost the total amount of this fill
  /// @fill_id the unique identifier of this fill
  virtual void fill(uint32_t fill_qty,
                    uint32_t fill_cost,
                    uint32_t fill_id);

  /// @brief get order conditions as a bit mask
  virtual book::OrderConditions conditions() const;

  /// @brief if no trades should happen until the order
  /// can be filled completely.
  /// Note: one or more trades may be used to fill the order.
  virtual bool all_or_none() const;

  /// @brief After generating as many trades as possible against
  /// orders already on the market, cancel any remaining quantity.
  virtual bool immediate_or_cancel() const;

  /// @brief exchange accepted this order
  void accept();
  /// @brief exchange cancelled this order
  void cancel();

  /// @brief exchange replaced this order
  /// @param size_delta change to the order quantity
  /// @param new_price the new price
  void replace(uint32_t size_delta, uint32_t new_price);

private:
  OrderState state_;
  bool is_buy_;
  uint32_t order_qty_;
  uint32_t price_;
  uint32_t stop_price_;
  book::OrderConditions conditions_;
  uint32_t filled_qty_;
  uint32_t filled_cost_;
  int32_t reserved_;            // quantity tentatively earmarked during AON planning (not yet filled)
  inline static uint32_t last_order_id_{0};

public:
  const uint32_t order_id_;
};

}

#endif // BABOMATCHINGENGINE_SIMPLE_ORDER_H
