//
// Reworked matching core: the book owns nothing; the narb_trees (global PIN chains)
// own all resting/parked orders by value. The application interacts purely by order id.
// Composes a book::Depth<SIZE> aggregate tracker for (future) top-of-book visualization.
//
#ifndef BABOMATCHINGENGINE_MATCHING_BOOK_H
#define BABOMATCHINGENGINE_MATCHING_BOOK_H

#include "../data_structures/narb_tree.h"
#include "../simple/simple_order.h"
#include "types.h"
#include "depth.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace babo {

// A single execution: `maker` was resting, `taker` was the incoming aggressor.
struct trade
{
    std::uint32_t maker_id;
    std::uint32_t taker_id;
    std::uint32_t qty;
    std::uint64_t price;   // executed at the maker's (resting) price
};

template <int SIZE = 5>
class matching_book
{
public:
    // Submit an order. Stop orders whose trigger hasn't been reached are parked; otherwise
    // the order matches the opposite side (market crosses everything) and any remainder rests.
    void add(simple::SimpleOrder order)
    {
        trades_.clear();
        if (order.stop_price() != 0 && is_stopped(order))
        {
            park_stop(order);            // held until the market reaches its stop price
            return;
        }
        submit(order);                   // match + rest (updates market price via trades)
        process_triggered();             // resubmit any stops the trades just triggered
    }

    // Cancel by id: resting book (feeds depth) or a parked stop (not in depth).
    void cancel(std::uint32_t order_id)
    {
        if (cancel_side(order_id, bids_, /*is_buy=*/true))  return;
        if (cancel_side(order_id, asks_, /*is_buy=*/false)) return;
        stopBids_.erase(order_id);       // parked stops: never in depth
        stopAsks_.erase(order_id);
    }

    // Replace/modify a resting order by id: change size (delta) and/or price, keeping the
    // same id. Implemented as cancel + re-submit, so a price change or size increase loses
    // time priority (re-enters at the back of its new level) -- standard exchange behaviour.
    // A repriced order that now crosses will match. new_price == PRICE_UNCHANGED (0) keeps price.
    void replace(std::uint32_t order_id, std::int32_t size_delta, std::uint64_t new_price)
    {
        trades_.clear();
        simple::SimpleOrder copy;
        if (simple::SimpleOrder* o = bids_.find_order(order_id))
        {
            copy = *o;
            depth_.close_order(static_cast<std::uint32_t>(o->price()), o->open_qty(), true);
            bids_.erase(order_id);
        }
        else if (simple::SimpleOrder* o2 = asks_.find_order(order_id))
        {
            copy = *o2;
            depth_.close_order(static_cast<std::uint32_t>(o2->price()), o2->open_qty(), false);
            asks_.erase(order_id);
        }
        else
        {
            return;   // not resting (unknown / parked stop / already gone)
        }

        copy.modify(size_delta, new_price);
        if (copy.open_qty() == 0) { depth_.published(); return; }   // reduced to nothing -> cancelled
        submit(copy);              // re-enter at the new price/qty (may now cross)
        process_triggered();
    }

    // Externally set the market price (e.g. from a reference feed); triggers crossed stops.
    void set_market_price(std::uint64_t px)
    {
        marketPrice_ = px;
        process_triggered();
    }

    [[nodiscard]] std::uint64_t market_price() const noexcept { return marketPrice_; }
    [[nodiscard]] const std::vector<trade>& last_trades() const noexcept { return trades_; }
    [[nodiscard]] narb_tree<order_type::BID>& bids() noexcept { return bids_; }
    [[nodiscard]] narb_tree<order_type::ASK>& asks() noexcept { return asks_; }
    [[nodiscard]] book::Depth<SIZE>&       depth()       noexcept { return depth_; }
    [[nodiscard]] const book::Depth<SIZE>& depth() const noexcept { return depth_; }

private:
    // Match against the opposite side, then rest the remainder (unless market).
    void submit(simple::SimpleOrder& order)
    {
        const bool is_market = (order.price() == book::MARKET_ORDER_PRICE);
        if (order.is_buy()) match_against(order, asks_);
        else                match_against(order, bids_);

        // Rest the remainder unless it's a market or IOC order (those never rest). A plain
        // AON that couldn't fill traded nothing, so it rests here as a resting AON.
        if (!is_market && !order.immediate_or_cancel() && order.open_qty() > 0)
        {
            const std::uint32_t rest_qty = order.open_qty();
            const std::uint64_t px       = order.price();
            const bool          is_buy   = order.is_buy();
            if (is_buy) bids_.insert(order);
            else        asks_.insert(order);
            depth_.add_order(static_cast<std::uint32_t>(px), rest_qty, is_buy);
        }
        depth_.published();
    }

    // A resting maker at its price crosses the incoming (a market incoming crosses everything).
    static bool crosses(const simple::SimpleOrder& maker, const simple::SimpleOrder& in) noexcept
    {
        if (in.price() == book::MARKET_ORDER_PRICE) return true;
        return in.is_buy() ? (maker.price() <= in.price())
                           : (maker.price() >= in.price());
    }

    // Execute one fill between the incoming order and a resting maker. Updates the maker's
    // level depth, market price and trade log. Returns whether the maker is now fully filled.
    bool execute_trade(simple::SimpleOrder& in, simple::SimpleOrder& maker, std::uint32_t qty)
    {
        const std::uint64_t px   = maker.price();
        const std::uint32_t cost = qty * static_cast<std::uint32_t>(px);
        in.fill(qty, cost, 0);
        maker.fill(qty, cost, 0);
        maker._level->_quantity -= qty;
        const bool maker_filled = (maker.open_qty() == 0);
        depth_.fill_order(static_cast<std::uint32_t>(px), qty, maker_filled, maker.is_buy());
        trades_.push_back({maker.order_id_, in.order_id_, qty, px});
        marketPrice_ = px;                                     // last trade sets the market price
        return maker_filled;
    }

    template <class OppTree>
    void match_against(simple::SimpleOrder& in, OppTree& opp)
    {
        if (in.all_or_none()) match_aon_incoming(in, opp);
        else                  match_regular_incoming(in, opp);
    }

    // Regular / IOC incoming: trade greedily best-first; skip any resting AON we can't fully
    // fill (trade around it -- it stays resting).
    template <class OppTree>
    void match_regular_incoming(simple::SimpleOrder& in, OppTree& opp)
    {
        auto it = opp.orders_begin();
        while (it != opp.orders_end() && in.open_qty() > 0)
        {
            simple::SimpleOrder& maker = *it;
            if (!crosses(maker, in)) break;
            if (maker.all_or_none() && maker.open_qty() > in.open_qty()) { ++it; continue; }

            const std::uint32_t qty = (std::min)(in.open_qty(), maker.open_qty());
            const std::uint32_t id  = maker.order_id_;
            ++it;                                          // advance before a possible erase
            if (execute_trade(in, maker, qty)) opp.erase(id);
        }
    }

    // AON incoming: all-or-nothing. Pass 1 plans a feasible full fill (skipping resting AONs
    // that can't fit within the remaining need); if the whole quantity can be met, Pass 2
    // commits it. Otherwise nothing trades (the order rests as a resting AON, or cancels if IOC).
    template <class OppTree>
    void match_aon_incoming(simple::SimpleOrder& in, OppTree& opp)
    {
        struct cand { std::uint32_t id; std::uint32_t qty; };
        std::vector<cand> plan;
        const std::uint32_t needed = in.open_qty();
        std::uint32_t planned = 0;

        for (auto it = opp.orders_begin(); it != opp.orders_end() && planned < needed; ++it)
        {
            simple::SimpleOrder& maker = *it;
            if (!crosses(maker, in)) break;
            const std::uint32_t avail = maker.open_qty();
            if (maker.all_or_none())
            {
                if (avail <= needed - planned) { plan.push_back({maker.order_id_, avail}); planned += avail; }
                // else: can't fully fill this resting AON within our need -> skip it
            }
            else
            {
                const std::uint32_t take = (std::min)(avail, needed - planned);
                plan.push_back({maker.order_id_, take});
                planned += take;
            }
        }

        if (planned < needed) return;   // infeasible -> AON does not trade at all

        for (const cand& c : plan)      // commit: makers are unchanged since planning
        {
            simple::SimpleOrder* maker = opp.find_order(c.id);
            if (!maker) continue;
            if (execute_trade(in, *maker, c.qty)) opp.erase(c.id);
        }
    }

    template <class SideTree>
    bool cancel_side(std::uint32_t order_id, SideTree& side, bool is_buy)
    {
        simple::SimpleOrder* o = side.find_order(order_id);
        if (!o) return false;
        depth_.close_order(static_cast<std::uint32_t>(o->price()), o->open_qty(), is_buy);
        side.erase(order_id);
        return true;
    }

    // --- stop orders ---
    // A stop is "stopped" (parked) until the market reaches its trigger:
    //   buy stop  fires when market rises to/above stop_price;
    //   sell stop fires when market falls to/below stop_price.
    [[nodiscard]] bool is_stopped(const simple::SimpleOrder& o) const noexcept
    {
        return o.is_buy() ? (marketPrice_ < o.stop_price())
                          : (marketPrice_ > o.stop_price());
    }

    void park_stop(simple::SimpleOrder& o)
    {
        // Keyed by STOP price. Ordering chosen so the next-to-trigger is "best":
        //   buy stops  -> ASK order (lowest stop first, triggered as market rises);
        //   sell stops -> BID order (highest stop first, triggered as market falls).
        if (o.is_buy()) stopBids_.insert(o, o.stop_price());
        else            stopAsks_.insert(o, o.stop_price());
    }

    // Repeatedly trigger crossed stops and resubmit them until the market stabilizes.
    // (Resubmitting can trade -> move the market -> trigger more stops.)
    void process_triggered()
    {
        for (;;)
        {
            std::vector<simple::SimpleOrder> fired;

            // Buy stops: fire while stop_price <= market (sweep lowest-first, stop above market).
            for (auto it = stopBids_.orders_begin(); it != stopBids_.orders_end(); )
            {
                if (it->stop_price() > marketPrice_) break;
                const std::uint32_t id = it->order_id_;
                simple::SimpleOrder copy = *it;
                ++it;
                stopBids_.erase(id);
                fired.push_back(copy);
            }
            // Sell stops: fire while stop_price >= market (sweep highest-first, stop below market).
            for (auto it = stopAsks_.orders_begin(); it != stopAsks_.orders_end(); )
            {
                if (it->stop_price() < marketPrice_) break;
                const std::uint32_t id = it->order_id_;
                simple::SimpleOrder copy = *it;
                ++it;
                stopAsks_.erase(id);
                fired.push_back(copy);
            }

            if (fired.empty()) break;
            for (auto& o : fired) submit(o);   // now live: match + rest (may move the market)
        }
    }

    narb_tree<order_type::BID> bids_;      // resting buy orders (by value)
    narb_tree<order_type::ASK> asks_;      // resting sell orders
    narb_tree<order_type::ASK> stopBids_;  // parked BUY stops, keyed by stop price (lowest first)
    narb_tree<order_type::BID> stopAsks_;  // parked SELL stops, keyed by stop price (highest first)
    book::Depth<SIZE> depth_;              // aggregate top-of-book view
    std::vector<trade> trades_;            // executions from the most recent add()
    std::uint64_t marketPrice_ = 0;        // last trade / externally set reference price
};

} // namespace babo

#endif // BABOMATCHINGENGINE_MATCHING_BOOK_H
