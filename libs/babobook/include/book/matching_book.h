//
// Reworked matching core: the book owns four narb_trees; each tree owns its
// resting/parked orders by value and returns its shared-pool allocations on destruction.
// The application interacts purely by order id.
// Composes a pull-based book::Depth<SIZE> snapshot and emits canonical order-domain
// events through one id-based OrderListener.
//
#ifndef BABOMATCHINGENGINE_MATCHING_BOOK_H
#define BABOMATCHINGENGINE_MATCHING_BOOK_H

#include "../data_structures/narb_tree.h"
#include "simple_order.h"
#include "types.h"
#include "depth.h"
#include "order_listener.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace babo::book {

template <int SIZE = 5>
class matching_book
{
public:
    using DepthSnapshot = book::Depth<SIZE>;

    using OrderEventListener = book::OrderListener<std::uint32_t>;

    void set_order_listener(OrderEventListener* l) noexcept { order_listener_ = l; }

    void add(simple::SimpleOrder order)
    {
        if (order.order_qty() == 0)
        {
            if (order_listener_) order_listener_->on_reject(order.order_id_, "size must be positive");
            return;
        }
        if (order_listener_) order_listener_->on_accept(order.order_id_);

        if (order.stop_price() != 0 && is_stopped(order))
            park_stop(order);                                  // held until the market reaches it
        else
        {
            submit(order);                                     // match + rest (moves market via trades)
            process_triggered();                               // resubmit any stops the trades triggered
        }
    }

    void cancel(std::uint32_t order_id)
    {
        bool found = cancel_side(order_id, bids_, /*is_buy=*/true)
                  || cancel_side(order_id, asks_, /*is_buy=*/false);
        if (!found)
        {
            if      (stopBids_.find_order(order_id)) { stopBids_.erase(order_id); found = true; }
            else if (stopAsks_.find_order(order_id)) { stopAsks_.erase(order_id); found = true; }
        }
        if (found)
        {
            if (order_listener_) order_listener_->on_cancel(order_id);
        }
        else if (order_listener_)
        {
            order_listener_->on_cancel_reject(order_id, "not found");
        }
    }

    void replace(std::uint32_t order_id, std::int32_t size_delta, std::uint64_t new_price)
    {
        // Locate the resting order and its side (bids_ and asks_ are distinct types -> branch).
        bool is_buy = true;
        simple::SimpleOrder* o = bids_.find_order(order_id);
        if (!o) { o = asks_.find_order(order_id); is_buy = false; }
        if (!o)
        {
            if (order_listener_) order_listener_->on_replace_reject(order_id, "not found");
            return;
        }

        const std::uint64_t cur_price = o->price();
        const bool reprice = (new_price != book::PRICE_UNCHANGED && new_price != cur_price);

        if (!reprice)
        {
            const std::uint32_t open = o->open_qty();
            if (size_delta < 0 && static_cast<std::uint32_t>(-size_delta) >= open)
            {
                if (is_buy) bids_.erase(order_id); else asks_.erase(order_id);
            }
            else
            {
                o->modify(size_delta, book::PRICE_UNCHANGED);   // order_qty_ += size_delta
                o->_level->_quantity = static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(o->_level->_quantity) + size_delta);
            }
            if (order_listener_)
                order_listener_->on_replace(order_id, size_delta, static_cast<std::uint32_t>(cur_price));
            return;
        }

        simple::SimpleOrder copy = *o;
        if (is_buy) bids_.erase(order_id); else asks_.erase(order_id);

        copy.modify(size_delta, new_price);
        if (order_listener_)
            order_listener_->on_replace(order_id, size_delta, static_cast<std::uint32_t>(copy.price()));

        if (copy.open_qty() == 0) return;
        submit(copy);
        process_triggered();
    }

    void set_market_price(std::uint64_t px)
    {
        marketPrice_ = px;
        process_triggered();
    }

    [[nodiscard]] std::uint64_t market_price() const noexcept { return marketPrice_; }

    [[nodiscard]] narb_tree<order_type::BID>& bids() noexcept { return bids_; }
    [[nodiscard]] narb_tree<order_type::ASK>& asks() noexcept { return asks_; }

    [[nodiscard]] DepthSnapshot& depth() noexcept
    {
        depth_.clear();
        fill_depth_side(bids_, depth_.bids());
        fill_depth_side(asks_, depth_.asks());
        return depth_;
    }
private:
    void submit(simple::SimpleOrder& order)
    {
        const bool is_market = (order.price() == book::MARKET_ORDER_PRICE);
        if (order.is_buy()) match_against(order, asks_);
        else                match_against(order, bids_);

        if (!is_market && !order.immediate_or_cancel() && order.open_qty() > 0)
        {
            if (order.is_buy()) bids_.insert(order);
            else                asks_.insert(order);
        }
    }

    template <class SideTree>
    static void fill_depth_side(SideTree& tree, book::DepthLevel* out) noexcept
    {
        int i = 0;
        for (auto it = tree.begin(); it != tree.end() && i < SIZE; ++it, ++i)
            out[i].set(static_cast<std::uint32_t>(it->_price), it->_quantity, it->_count);
    }
    static bool crosses(const simple::SimpleOrder& maker, const simple::SimpleOrder& in) noexcept
    {
        if (in.price() == book::MARKET_ORDER_PRICE) return true;
        return in.is_buy() ? (maker.price() <= in.price())
                           : (maker.price() >= in.price());
    }

    bool execute_trade(simple::SimpleOrder& in, simple::SimpleOrder& maker, std::uint32_t qty)
    {
        const std::uint64_t px   = maker.price();
        const std::uint32_t cost = qty * static_cast<std::uint32_t>(px);
        in.fill(qty, cost, 0);
        maker.fill(qty, cost, 0);
        maker._level->_quantity -= qty;
        const bool maker_filled = (maker.open_qty() == 0);
        marketPrice_ = px;

        if (order_listener_) order_listener_->on_fill(in.order_id_, maker.order_id_, qty, cost);
        return maker_filled;
    }

    template <class OppTree>
    void match_against(simple::SimpleOrder& in, OppTree& opp)
    {
        if (in.all_or_none()) match_aon_incoming(in, opp);
        else                  match_regular_incoming(in, opp);
    }

    // Regular / IOC incoming: trade greedily best-first; skip a resting AON we can't fully fill.
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
            ++it;
            if (execute_trade(in, maker, qty)) opp.erase(id);
        }
    }

    // AON incoming: plan a feasible full fill, then commit it -- or trade nothing.
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
            }
            else
            {
                const std::uint32_t take = (std::min)(avail, needed - planned);
                plan.push_back({maker.order_id_, take});
                planned += take;
            }
        }

        if (planned < needed) return;   // infeasible -> AON does not trade at all

        for (const cand& c : plan)
        {
            simple::SimpleOrder* maker = opp.find_order(c.id);
            if (!maker) continue;
            if (execute_trade(in, *maker, c.qty)) opp.erase(c.id);
        }
    }

    template <class SideTree>
    bool cancel_side(std::uint32_t order_id, SideTree& side, [[maybe_unused]] bool is_buy)
    {
        simple::SimpleOrder* o = side.find_order(order_id);
        if (!o) return false;
        side.erase(order_id);
        return true;
    }

    [[nodiscard]] bool is_stopped(const simple::SimpleOrder& o) const noexcept
    {
        return o.is_buy() ? (marketPrice_ < o.stop_price())
                          : (marketPrice_ > o.stop_price());
    }

    void park_stop(simple::SimpleOrder& o)
    {
        if (o.is_buy()) stopBids_.insert(o, o.stop_price());   // ASK order: lowest stop first
        else            stopAsks_.insert(o, o.stop_price());   // BID order: highest stop first
    }

    void process_triggered()
    {
        for (;;)
        {
            std::vector<simple::SimpleOrder> fired;
            for (auto it = stopBids_.orders_begin(); it != stopBids_.orders_end(); )
            {
                if (it->stop_price() > marketPrice_) break;
                const std::uint32_t id = it->order_id_;
                simple::SimpleOrder copy = *it;
                ++it;
                stopBids_.erase(id);
                fired.push_back(copy);
            }
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
    book::Depth<SIZE> depth_;              // top-of-book snapshot, rebuilt on depth() (pull-based)

    OrderEventListener* order_listener_ = nullptr;
    std::uint64_t marketPrice_ = 0;        // last trade / externally set reference price
};

} // namespace babo

#endif // BABOMATCHINGENGINE_MATCHING_BOOK_H
