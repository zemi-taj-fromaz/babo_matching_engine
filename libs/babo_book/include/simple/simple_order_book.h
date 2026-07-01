#ifndef BABOMATCHINGENGINE_SIMPLE_ORDER_BOOK_H
#define BABOMATCHINGENGINE_SIMPLE_ORDER_BOOK_H


#include "simple_order.h"
#include <book/depth_order_book.h>
#include <iostream>

namespace babo::simple {

// @brief binding of DepthOrderBook template with SimpleOrder* order pointer.
template <int SIZE = 5>
class SimpleOrderBook : public book::DepthOrderBook<SimpleOrder*, SIZE> {
public:
  typedef book::Callback<SimpleOrder*> SimpleCallback;
  typedef uint32_t uint32_t;

  SimpleOrderBook();

  // Override callback handling to update SimpleOrder state
  virtual void perform_callback(SimpleCallback& cb);
private:
  uint32_t fill_id_;
};

template <int SIZE>
SimpleOrderBook<SIZE>::SimpleOrderBook()
: fill_id_(0)
{
}

template <int SIZE>
inline void
SimpleOrderBook<SIZE>::perform_callback(SimpleCallback& cb)
{
  book::DepthOrderBook<SimpleOrder*, SIZE>::perform_callback(cb);
  switch(cb.type) {
  case SimpleCallback::cb_order_accept:
    cb.order->accept();
    break;
  case SimpleCallback::cb_order_fill: {
    // Increment fill ID once.
    ++fill_id_;
    // NOTE: orders are now filled in place during matching (create_trade),
    // since the order is its own tracker. Filling again here would double-count.
    break;
  }
  case SimpleCallback::cb_order_cancel:
    cb.order->cancel();
    break;
  case SimpleCallback::cb_order_replace:
    // Modify the order itself
    cb.order->replace(cb.delta, cb.price);
    break;
  default:
    // Nothing
    break;
  }
}
}

#endif // BABOMATCHINGENGINE_SIMPLE_ORDER_BOOK_H
