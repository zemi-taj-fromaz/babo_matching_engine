// Selects the liquibook book type for both the adapter and the perf binary, so a
// depth-ON and a depth-OFF build can be produced from the same source by flipping
// one compile-time macro:
//
//   default            -> SimpleOrderBook<5>  (depth ON: stock liquibook, maintains
//                         an aggregate Depth<5> on every accept/fill/cancel/replace)
//   -DLIQUI_NO_DEPTH    -> NoDepthBook         (depth OFF: for a fair throughput
//                         comparison against babo's depth-off build)
//
// NoDepthBook is a verbatim copy of SimpleOrderBook<SIZE>::perform_callback with a
// SINGLE change: the base call goes to OrderBook instead of DepthOrderBook -- the
// only depth-bearing line. Everything the consumers depend on is preserved:
//   * the SimpleOrder state bookkeeping (accept/fill/cancel/replace) that the
//     adapter's resting() reads via state()/open_qty();
//   * the order/trade LISTENER reports (dispatched by OrderBook::perform_callback),
//     which drive the correctness hash.
// Only the per-op Depth<SIZE> maintenance is dropped. Adapter-local: the vendored
// liquibook sources and the liqui_unit baseline are untouched. NOTE: the depth-OFF
// build changes the baseline's *work*, not its *reports* -- rerun regen_references
// to confirm the canonical hashes are byte-identical before trusting a comparison.
#pragma once

#include <simple/simple_order.h>
#include <simple/simple_order_book.h>

#include <cstdint>

namespace babo_bench {

namespace lb = liquibook::book;
using LO = liquibook::simple::SimpleOrder;

#ifdef LIQUI_NO_DEPTH
class NoDepthBook : public lb::OrderBook<LO*> {
public:
    void perform_callback(lb::Callback<LO*>& cb) override {
        lb::OrderBook<LO*>::perform_callback(cb);   // was DepthOrderBook:: (the +depth line)
        switch (cb.type) {
            case lb::Callback<LO*>::cb_order_accept:
                cb.order->accept();
                break;
            case lb::Callback<LO*>::cb_order_fill: {
                ++fill_id_;
                const lb::Cost fill_cost = cb.quantity * cb.price;
                cb.matched_order->fill(cb.quantity, fill_cost, fill_id_);
                cb.order->fill(cb.quantity, fill_cost, fill_id_);
                break;
            }
            case lb::Callback<LO*>::cb_order_cancel:
                cb.order->cancel();
                break;
            case lb::Callback<LO*>::cb_order_replace:
                cb.order->replace(cb.delta, cb.price);
                break;
            default:
                break;
        }
    }
private:
    uint32_t fill_id_ = 0;
};
using LiquiBook = NoDepthBook;
#else
using LiquiBook = liquibook::simple::SimpleOrderBook<5>;
#endif

} // namespace babo_bench
