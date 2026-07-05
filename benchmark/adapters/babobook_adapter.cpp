//
// babobook_adapter.cpp — matching_engine_api.h backed by babo's matching_book.
//
// babo is id-based: add() takes a SimpleOrder by value, and cancel / find_order /
// the OrderListener are all keyed on the order id. So unlike the Liquibook
// adapter this needs NO id->pointer forward map and NO internal-id->harness-id
// reverse map: we stamp SimpleOrder::order_id_ with the harness order id before
// add(), and babo's book itself is the lookup table. on_fill hands back those
// same ids, so trades are labeled with zero extra bookkeeping.
//
// Report derivation (adapter-owned, no engine change): OrderAck per new order,
// Trade per fill (via the listener), CancelAck per cancel and per IOC residual,
// ModifyAck per modify, and CancelReject / ModifyReject for a cancel/modify of an
// order that is not resting. babo matches synchronously, so engine_flush() is a
// no-op. Modify is cancel + reinsert (the harness rule); babo's replace() is not
// used because it keeps time priority on a same-price change.
#include "../api/matching_engine_api.h"

#include <book/matching_book.h>
#include <simple/simple_order.h>

#include <cstdint>

#if defined(__aarch64__) || defined(_M_ARM64)
#  if defined(_MSC_VER)
#    include <intrin.h>
static inline void cpu_pause() { __yield(); }
#  else
static inline void cpu_pause() { asm volatile("yield" ::: "memory"); }
#  endif
#elif defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#  include <immintrin.h>
static inline void cpu_pause() { _mm_pause(); }
#else
static inline void cpu_pause() {}
#endif

namespace {

using Order = babo::simple::SimpleOrder;
using Book  = babo::book::matching_book<>;        // default depth (5)
namespace bk = babo::book;

Book* g_book = nullptr;

const me_transport_t* g_transport = nullptr;      // harness report transport
void*                 g_sink      = nullptr;

uint64_t g_seq    = 0;    // aggressive order's sequence number (current call)
uint64_t g_filled = 0;    // quantity the aggressive order filled this call

void push_report(const me_report_t& r) {
    while (!g_transport->push(g_sink, &r)) cpu_pause();
}

// Emit a non-trade report (OrderAck / CancelAck / ModifyAck / CancelReject /
// ModifyReject).
void emit_ack(uint8_t type, uint64_t seq, uint64_t order_id,
              uint8_t side, int64_t price, uint32_t qty) {
    me_report_t r{};
    r.type            = type;
    r.sequence_number = seq;
    r.order_id        = order_id;
    r.side            = side;
    r.price_ticks     = price;
    r.quantity        = qty;
    push_report(r);
}

// babo reports fills through this listener during add(). Only on_fill does work;
// the acks are emitted explicitly in engine_on_* so we control their sequencing.
// on_fill's ids are already the harness order ids (we stamp order_id_), and it
// hands us COST (= qty * price), so the maker's resting price is cost / qty.
class Listener : public bk::OrderListener<std::uint32_t> {
public:
    void on_fill(const std::uint32_t& taker, const std::uint32_t& maker,
                 std::uint32_t qty, std::uint32_t cost) override {
        me_report_t r{};
        r.type            = ME_TRADE;
        r.sequence_number = g_seq;
        r.price_ticks     = static_cast<int64_t>(cost / qty);   // maker's resting price
        r.quantity        = qty;
        r.maker_order_id  = maker;   // already the harness id — no reverse map
        r.taker_order_id  = taker;
        push_report(r);
        g_filled += qty;
    }
    void on_accept(const std::uint32_t&) override {}
    void on_reject(const std::uint32_t&, const char*) override {}
    void on_cancel(const std::uint32_t&) override {}
    void on_cancel_reject(const std::uint32_t&, const char*) override {}
    void on_replace(const std::uint32_t&, const std::int32_t&, std::uint32_t) override {}
    void on_replace_reject(const std::uint32_t&, const char*) override {}
};
Listener g_listener;

// Find a resting order by id; returns it (and its side) or nullptr if not
// resting (an order in the tree is by definition resting — filled ones are erased).
Order* find_resting(std::uint32_t id, bool& is_buy) {
    if (Order* o = g_book->bids().find_order(id)) { is_buy = true;  return o; }
    if (Order* o = g_book->asks().find_order(id)) { is_buy = false; return o; }
    return nullptr;
}

}  // namespace

extern "C" {

void engine_init(uint64_t /*seed*/, const me_transport_t* transport,
                 void* report_sink) {
    g_transport = transport;
    g_sink      = report_sink;
    g_book = new Book();
    g_book->set_order_listener(&g_listener);
}

void engine_shutdown(void) {
    delete g_book;            // babo owns every order by value in its own pools
    g_book = nullptr;
}

// babo matches synchronously on the calling thread — nothing is pending.
void engine_flush(void) {}

void engine_on_new_order(const new_order_t* o) {
    g_seq    = o->sequence_number;
    g_filled = 0;
    emit_ack(ME_ORDER_ACK, o->sequence_number, o->order_id,
             o->side, o->price_ticks, o->quantity);

    // Build the native order and stamp it with the HARNESS id, so cancel/find and
    // on_fill all speak that id. babo copies it into the book by value.
    Order ord(o->side == 0, static_cast<uint32_t>(o->price_ticks), o->quantity,
              0, o->ioc ? bk::oc_immediate_or_cancel : bk::oc_no_conditions);
    ord.order_id_ = static_cast<uint32_t>(o->order_id);
    g_book->add(ord);   // fills delivered as Trade reports via Listener::on_fill

    if (o->ioc && g_filled < o->quantity)              // IOC residual cancellation
        emit_ack(ME_CANCEL_ACK, o->sequence_number, o->order_id, o->side,
                 o->price_ticks, static_cast<uint32_t>(o->quantity - g_filled));
}

void engine_on_cancel(const cancel_t* c) {
    bool is_buy = true;
    Order* o = find_resting(static_cast<uint32_t>(c->order_id), is_buy);
    if (o) {
        const int64_t px = static_cast<int64_t>(o->price());
        g_book->cancel(static_cast<uint32_t>(c->order_id));
        emit_ack(ME_CANCEL_ACK, c->sequence_number, c->order_id,
                 is_buy ? 0 : 1, px, 0);
    } else {
        // Not resting — already filled, already cancelled, or never seen: reject.
        emit_ack(ME_CANCEL_REJECT, c->sequence_number, c->order_id, 0, 0, 0);
    }
}

void engine_on_modify(const modify_t* m) {
    g_seq    = m->sequence_number;
    g_filled = 0;
    bool is_buy = true;
    Order* cur = find_resting(static_cast<uint32_t>(m->order_id), is_buy);
    if (cur) {
        // Cancel + reinsert at the new price/qty (loses queue priority — the
        // harness's modify rule). babo's replace() edits a same-price change in
        // place and KEEPS priority, so it is deliberately not used.
        g_book->cancel(static_cast<uint32_t>(m->order_id));
        Order n(m->side == 0, static_cast<uint32_t>(m->new_price_ticks), m->new_quantity);
        n.order_id_ = static_cast<uint32_t>(m->order_id);
        g_book->add(n);   // crossing fills -> Trade reports
        emit_ack(ME_MODIFY_ACK, m->sequence_number, m->order_id,
                 m->side, m->new_price_ticks, m->new_quantity);
    } else {
        emit_ack(ME_MODIFY_REJECT, m->sequence_number, m->order_id, 0, 0, 0);
    }
}

int64_t engine_query_best_bid(void) {
    auto* lvl = g_book->bids().get_best();
    return lvl ? static_cast<int64_t>(lvl->_price) : INT64_MIN;
}

int64_t engine_query_best_ask(void) {
    auto* lvl = g_book->asks().get_best();
    return lvl ? static_cast<int64_t>(lvl->_price) : INT64_MAX;
}

uint64_t engine_query_depth_at(int64_t price_ticks, uint8_t side) {
    // babo's aggregate depth() tracks only the top levels, so to answer ANY price
    // we sum open qty over that side's resting orders. Called only at audit
    // probes (bids() and asks() are distinct types, hence the branch).
    const uint32_t p = static_cast<uint32_t>(price_ticks);
    uint64_t total = 0;
    if (side == 0) {
        auto& t = g_book->bids();
        for (auto it = t.orders_begin(); it != t.orders_end(); ++it)
            if (it->price() == p) total += it->open_qty();
    } else {
        auto& t = g_book->asks();
        for (auto it = t.orders_begin(); it != t.orders_end(); ++it)
            if (it->price() == p) total += it->open_qty();
    }
    return total;
}

// Optional batch delivery: process a run of messages in one cross-.so call,
// looping the per-message handlers. Same strict in-order semantics as
// one-at-a-time delivery — removes only the per-message dispatch overhead.
void engine_on_batch(const me_msg_t* msgs, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        const me_msg_t& m = msgs[i];
        if (m.type == 0)      engine_on_new_order(&m.no);
        else if (m.type == 1) engine_on_cancel(&m.c);
        else                  engine_on_modify(&m.md);
    }
}

}  // extern "C"
