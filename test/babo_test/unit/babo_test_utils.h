// babo-native test harness.
//
// Liquibook's ut_utils.h (add_and_verify / FillCheck / replace_and_verify) works
// because the book mutates the *caller's* order object, so you can inspect
// order->filled_qty()/state() afterward. babo's matching_book stores orders BY
// VALUE, so the caller's SimpleOrder is stale the moment it's added. We therefore
// observe results through three channels instead:
//   1. RecordingListener  -- accept/reject/fill/cancel/replace events, keyed by id
//   2. the resting copy     -- book.bids()/asks().find_order(id) for post-fill state
#pragma once

#include "depth_check.h"

#include <gtest/gtest.h>
#include <book/matching_book.h>
#include <book/order_listener.h>
#include <book/simple_order.h>

#include <cstdint>
#include <string>
#include <vector>

namespace babo::test {

using babo::book::matching_book;
using babo::simple::SimpleOrder;

// Records every order-listener callback so tests can assert on the event stream.
class RecordingListener : public babo::book::OrderListener<std::uint32_t> {
public:
  struct Fill    { std::uint32_t taker_id; std::uint32_t maker_id; std::uint32_t qty; std::uint32_t cost; };
  struct Replace { std::uint32_t id; std::int32_t size_delta; std::uint32_t new_price; };
  struct Reason  { std::uint32_t id; std::string reason; };

  std::vector<std::uint32_t> accepts;
  std::vector<std::uint32_t> cancels;
  std::vector<Fill>          fills;
  std::vector<Replace>       replaces;
  std::vector<Reason>        rejects;
  std::vector<Reason>        cancel_rejects;
  std::vector<Reason>        replace_rejects;

  void on_accept(const std::uint32_t& id) override { accepts.push_back(id); }
  void on_reject(const std::uint32_t& id, const char* r) override { rejects.push_back({id, r ? r : ""}); }
  void on_fill(const std::uint32_t& taker, const std::uint32_t& maker,
               std::uint32_t qty, std::uint32_t cost) override { fills.push_back({taker, maker, qty, cost}); }
  void on_cancel(const std::uint32_t& id) override { cancels.push_back(id); }
  void on_cancel_reject(const std::uint32_t& id, const char* r) override { cancel_rejects.push_back({id, r ? r : ""}); }
  void on_replace(const std::uint32_t& id, const std::int32_t& d, std::uint32_t p) override { replaces.push_back({id, d, p}); }
  void on_replace_reject(const std::uint32_t& id, const char* r) override { replace_rejects.push_back({id, r ? r : ""}); }

  void clear() {
    accepts.clear(); cancels.clear(); fills.clear(); replaces.clear();
    rejects.clear(); cancel_rejects.clear(); replace_rejects.clear();
  }

  // Total quantity `id` traded, whether it was the taker or a resting maker.
  std::uint32_t filled_qty(std::uint32_t id) const {
    std::uint32_t q = 0;
    for (const auto& f : fills)
      if (f.taker_id == id || f.maker_id == id) q += f.qty;
    return q;
  }

  bool was_accepted(std::uint32_t id) const { return contains(accepts, id); }
  bool was_cancelled(std::uint32_t id) const { return contains(cancels, id); }

private:
  static bool contains(const std::vector<std::uint32_t>& v, std::uint32_t id) {
    for (auto x : v) if (x == id) return true;
    return false;
  }
};

// Base fixture: a book wired to a RecordingListener, with deterministic order ids
// (reset to start at 1) per test. Parameterize on SIZE via the template.
template <int SIZE = 5>
class BookFixtureT : public ::testing::Test {
protected:
  using Book = matching_book<SIZE>;

  void SetUp() override {
    SimpleOrder::reset_id_generator();   // ids restart at 1 each test
    book.set_order_listener(&listener);
  }

  // Is `id` still resting somewhere in the visible book?
  bool resting(std::uint32_t id) {
    return book.bids().find_order(id) != nullptr
        || book.asks().find_order(id) != nullptr;
  }

  // Open (unfilled, resting) quantity of `id`, or 0 if it isn't resting.
  std::uint32_t resting_qty(std::uint32_t id) {
    if (auto* o = book.bids().find_order(id)) return o->open_qty();
    if (auto* o = book.asks().find_order(id)) return o->open_qty();
    return 0;
  }

  Book             book;
  RecordingListener listener;
};

using BookFixture = BookFixtureT<5>;

} // namespace babo::test
