// Core matching tests for babo::book::matching_book.
//
// These are the liquibook ut_order_book.cpp scenarios, translated to GoogleTest
// and to babo's id/listener/by-value model (see unit/babo_test_utils.h for why
// the verification style differs from liquibook's FillCheck).
#include "unit/babo_test_utils.h"

#include <gtest/gtest.h>
#include <type_traits>

namespace babo::test {

using Fixture = BookFixture;   // matching_book<5> + RecordingListener, ids reset per test

TEST(NarbTreeLifetime, ReturnsLiveBookAllocationsToSharedPools)
{
  using Book = babo::book::matching_book<>;

  static_assert(!std::is_copy_constructible_v<Book>);
  static_assert(!std::is_copy_assignable_v<Book>);
  static_assert(!std::is_move_constructible_v<Book>);
  static_assert(!std::is_move_assignable_v<Book>);

  const auto pin_before = pin_node_pool().getNbElements();
  const auto level_before = price_level_descriptor_pool().getNbElements();

  {
    Book survivor;
    SimpleOrder survivor_bid(true, 80, 10);
    const auto survivor_id = survivor_bid.order_id_;
    survivor.add(survivor_bid);

    EXPECT_EQ(pin_node_pool().getNbElements(), pin_before + 1);
    EXPECT_EQ(price_level_descriptor_pool().getNbElements(), level_before + 1);

    {
      Book book;
      book.set_market_price(100);

      // One live order in each of the four independently owned narb_trees:
      // bids, asks, parked buy stops, and parked sell stops.
      book.add(SimpleOrder(true,   90, 10));
      book.add(SimpleOrder(false, 110, 10));
      book.add(SimpleOrder(true,   95, 10, 101));
      book.add(SimpleOrder(false, 105, 10,  99));

      EXPECT_EQ(pin_node_pool().getNbElements(), pin_before + 5);
      EXPECT_EQ(price_level_descriptor_pool().getNbElements(), level_before + 5);
    }

    // Destroying one book returned only its four trees' allocations.
    EXPECT_EQ(pin_node_pool().getNbElements(), pin_before + 1);
    EXPECT_EQ(price_level_descriptor_pool().getNbElements(), level_before + 1);
    EXPECT_NE(survivor.bids().find_order(survivor_id), nullptr);
  }

  EXPECT_EQ(pin_node_pool().getNbElements(), pin_before);
  EXPECT_EQ(price_level_descriptor_pool().getNbElements(), level_before);
}

// A resting sell is fully consumed by a matching buy; unrelated levels survive.
TEST_F(Fixture, AddCompleteBid)
{
  SimpleOrder ask1(false, 1252, 100);
  SimpleOrder ask0(false, 1251, 100);
  SimpleOrder bid1(true,  1251, 100);
  SimpleOrder bid0(true,  1250, 100);
  const auto ask0_id = ask0.order_id_;
  const auto bid1_id = bid1.order_id_;

  // No match on the way in.
  book.add(bid0);
  book.add(ask0);
  book.add(ask1);
  EXPECT_EQ(listener.fills.size(), 0u);

  {
    DepthCheck<Book> dc(book.depth());
    EXPECT_TRUE(dc.verify_bid(1250, 1, 100));
    EXPECT_TRUE(dc.verify_ask(1251, 1, 100));
    EXPECT_TRUE(dc.verify_ask(1252, 1, 100));
  }

  // bid1 @1251 crosses ask0 @1251 -> full fill at the maker's price.
  book.add(bid1);

  ASSERT_EQ(listener.fills.size(), 1u);
  const auto& f = listener.fills[0];
  EXPECT_EQ(f.taker_id, bid1_id);
  EXPECT_EQ(f.maker_id, ask0_id);
  EXPECT_EQ(f.qty,  100u);
  EXPECT_EQ(f.cost, 100u * 1251u);
  EXPECT_EQ(book.market_price(), 1251u);
  EXPECT_FALSE(resting(bid1_id));   // taker fully filled
  EXPECT_FALSE(resting(ask0_id));   // maker fully filled

  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_bid(1250, 1, 100));
  EXPECT_TRUE(dc.verify_ask(1252, 1, 100));
  EXPECT_TRUE(dc.verify_asks_done());
}

// Symmetric case: an incoming sell hits a resting buy.
TEST_F(Fixture, AddCompleteAsk)
{
  SimpleOrder ask0(false, 1251, 100);
  SimpleOrder ask1(false, 1250, 100);
  SimpleOrder bid0(true,  1250, 100);
  const auto ask1_id = ask1.order_id_;
  const auto bid0_id = bid0.order_id_;

  book.add(bid0);
  book.add(ask0);
  EXPECT_EQ(listener.fills.size(), 0u);

  book.add(ask1);   // ask1 @1250 crosses bid0 @1250

  ASSERT_EQ(listener.fills.size(), 1u);
  EXPECT_EQ(listener.fills[0].taker_id, ask1_id);
  EXPECT_EQ(listener.fills[0].maker_id, bid0_id);
  EXPECT_EQ(listener.fills[0].cost, 100u * 1250u);
  EXPECT_EQ(book.market_price(), 1250u);
  EXPECT_FALSE(resting(ask1_id));
  EXPECT_FALSE(resting(bid0_id));

  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_bids_done());        // bid consumed
  EXPECT_TRUE(dc.verify_ask(1251, 1, 100));  // ask0 untouched
}

// A larger incoming order sweeps several resting orders at the same price.
TEST_F(Fixture, AddMultiMatchBid)
{
  SimpleOrder ask1(false, 1252, 100);
  SimpleOrder ask0(false, 1251, 300);
  SimpleOrder ask2(false, 1251, 200);
  SimpleOrder bid1(true,  1251, 500);
  SimpleOrder bid0(true,  1250, 100);
  const auto ask0_id = ask0.order_id_;
  const auto ask2_id = ask2.order_id_;
  const auto bid1_id = bid1.order_id_;

  book.add(bid0);
  book.add(ask0);
  book.add(ask1);
  book.add(ask2);

  // bid1 wants 500 @1251; the 1251 level holds 300 (ask0, first) + 200 (ask2).
  book.add(bid1);

  ASSERT_EQ(listener.fills.size(), 2u);
  // Time priority: ask0 (added first) fills before ask2.
  EXPECT_EQ(listener.fills[0].maker_id, ask0_id);
  EXPECT_EQ(listener.fills[0].qty, 300u);
  EXPECT_EQ(listener.fills[1].maker_id, ask2_id);
  EXPECT_EQ(listener.fills[1].qty, 200u);
  EXPECT_EQ(listener.filled_qty(bid1_id), 500u);
  EXPECT_FALSE(resting(bid1_id));   // 500 fully filled, nothing left to rest
  EXPECT_EQ(book.market_price(), 1251u);

  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_bid(1250, 1, 100));
  EXPECT_TRUE(dc.verify_ask(1252, 1, 100));
  EXPECT_TRUE(dc.verify_asks_done());
}

// Incoming buy larger than available: remainder rests on the book as a new bid.
TEST_F(Fixture, PartialFillRestsRemainder)
{
  SimpleOrder ask(false, 10, 60);
  SimpleOrder bid(true,  10, 100);
  const auto ask_id = ask.order_id_;
  const auto bid_id = bid.order_id_;

  book.add(ask);
  book.add(bid);

  ASSERT_EQ(listener.fills.size(), 1u);
  EXPECT_EQ(listener.fills[0].qty, 60u);
  EXPECT_FALSE(resting(ask_id));    // maker fully filled
  EXPECT_TRUE(resting(bid_id));     // 40 remainder rests

  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_bid(10, 1, 40));   // remainder visible
  EXPECT_TRUE(dc.verify_asks_done());
}

// Non-crossing orders both rest; the spread is preserved.
TEST_F(Fixture, NoMatchBothRest)
{
  SimpleOrder bid(true,  9,  100);
  SimpleOrder ask(false, 10, 100);
  book.add(bid);
  book.add(ask);

  EXPECT_EQ(listener.fills.size(), 0u);
  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_bid(9,  1, 100));
  EXPECT_TRUE(dc.verify_ask(10, 1, 100));
}

// A crossing buy trades at the resting maker's price, not its own aggressive price.
TEST_F(Fixture, TradeExecutesAtMakerPrice)
{
  SimpleOrder ask(false, 10, 100);
  SimpleOrder bid(true,  12, 100);   // willing to pay 12
  book.add(ask);
  book.add(bid);

  ASSERT_EQ(listener.fills.size(), 1u);
  EXPECT_EQ(listener.fills[0].cost, 100u * 10u);   // executed at 10
  EXPECT_EQ(book.market_price(), 10u);
}

// Zero-quantity orders are rejected before ever touching the book.
TEST_F(Fixture, RejectZeroQuantity)
{
  SimpleOrder bad(true, 10, 0);
  const auto bad_id = bad.order_id_;
  book.add(bad);

  ASSERT_EQ(listener.rejects.size(), 1u);
  EXPECT_EQ(listener.rejects[0].id, bad_id);
  EXPECT_TRUE(listener.accepts.empty());
  EXPECT_FALSE(resting(bad_id));
}

} // namespace babo::test
