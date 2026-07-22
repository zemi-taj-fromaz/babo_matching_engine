//
// Created by Adminstudio on 6/25/2026.
//

#ifndef BABOMATCHINGENGINE_ORDER_LISTENER_H
#define BABOMATCHINGENGINE_ORDER_LISTENER_H

namespace babo::book {

//inherit this interface and set it as a listener in the matching book if you wish to react to events regarding orders
template <typename OrderPtr>
class OrderListener {
public:
  virtual void on_accept(const OrderPtr& order) = 0;
    virtual void on_reject(const OrderPtr& order, const char* reason) = 0;
  virtual void on_fill(const OrderPtr& order,
                       const OrderPtr& matched_order,
                       uint32_t fill_qty,
                       uint32_t fill_cost) = 0;
    virtual void on_cancel(const OrderPtr& order) = 0;
  virtual void on_cancel_reject(const OrderPtr& order, const char* reason) = 0;
  virtual void on_replace(const OrderPtr& order,
                          const int32_t& size_delta,
                          uint32_t new_price) = 0;
  virtual void on_replace_reject(const OrderPtr& order, const char* reason) = 0;
};

}

#endif // BABOMATCHINGENGINE_ORDER_LISTENER_H
