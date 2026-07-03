// Cancel and replace (modify) behaviour of matching_book.
//
// replace(id, size_delta, new_price):
//   * price unchanged  -> size edited in place, time priority retained;
//                         a reduction >= open qty collapses (removes) the order.
//   * price changed     -> cancel + re-submit (may cross and trade immediately).
#include "unit/babo_test_utils.h"

#include <gtest/gtest.h>

namespace babo::test {

using Fixture = BookFixture;

namespace { constexpr bool BUY = true, SELL = false; constexpr std::uint32_t UNCH = babo::book::PRICE_UNCHANGED; }

// Cancelling a resting order removes it and fires on_cancel.
TEST_F(Fixture, CancelResting)
{
  SimpleOrder bid(BUY, 10, 100);
  book.add(bid);
  ASSERT_TRUE(resting(bid.order_id_));

  book.cancel(bid.order_id_);

  EXPECT_TRUE(listener.was_cancelled(bid.order_id_));
  EXPECT_FALSE(resting(bid.order_id_));
  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_bids_done());
}

// Cancelling an unknown id is rejected, not silently ignored.
TEST_F(Fixture, CancelUnknownRejects)
{
  book.cancel(999);
  ASSERT_EQ(listener.cancel_rejects.size(), 1u);
  EXPECT_EQ(listener.cancel_rejects[0].id, 999u);
  EXPECT_EQ(listener.cancels.size(), 0u);
}

// A pure size increase grows the level and keeps time priority.
TEST_F(Fixture, ReplaceSizeIncreaseKeepsTimePriority)
{
  SimpleOrder a(SELL, 10, 100);   // added first -> higher time priority
  SimpleOrder b(SELL, 10, 100);
  book.add(a);
  book.add(b);

  book.replace(a.order_id_, +50, UNCH);        // a: 100 -> 150
  ASSERT_EQ(listener.replaces.size(), 1u);
  EXPECT_EQ(resting_qty(a.order_id_), 150u);
  { DepthCheck<Book> dc(book.depth()); EXPECT_TRUE(dc.verify_ask(10, 2, 250)); }

  // Incoming buy of 100 must still hit `a` first (priority retained).
  SimpleOrder buy(BUY, 10, 100);
  book.add(buy);
  ASSERT_EQ(listener.fills.size(), 1u);
  EXPECT_EQ(listener.fills[0].maker_id, a.order_id_);
  EXPECT_EQ(resting_qty(a.order_id_), 50u);     // 150 - 100
}

// A pure size decrease shrinks the level in place.
TEST_F(Fixture, ReplaceSizeDecrease)
{
  SimpleOrder bid(BUY, 10, 100);
  book.add(bid);

  book.replace(bid.order_id_, -40, UNCH);
  EXPECT_EQ(resting_qty(bid.order_id_), 60u);
  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_bid(10, 1, 60));
}

// A reduction that meets/exceeds the open qty collapses the order.
TEST_F(Fixture, ReplaceSizeReductionCollapses)
{
  SimpleOrder bid(BUY, 10, 100);
  book.add(bid);

  book.replace(bid.order_id_, -100, UNCH);
  EXPECT_GE(listener.replaces.size(), 1u);
  EXPECT_FALSE(resting(bid.order_id_));
  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_bids_done());
}

// A reprice that crosses the opposite side trades immediately (cancel + resubmit).
TEST_F(Fixture, ReplaceRepriceCrossesAndTrades)
{
  SimpleOrder ask(SELL, 11, 100);
  SimpleOrder bid(BUY,   9, 100);   // not crossing yet
  book.add(ask);
  book.add(bid);
  ASSERT_EQ(listener.fills.size(), 0u);

  book.replace(bid.order_id_, 0, 11);          // reprice bid up to 11 -> crosses ask
  ASSERT_EQ(listener.fills.size(), 1u);
  EXPECT_EQ(listener.fills[0].maker_id, ask.order_id_);
  EXPECT_EQ(listener.fills[0].cost, 100u * 11u);   // executed at price 11
  EXPECT_FALSE(resting(bid.order_id_));
  EXPECT_FALSE(resting(ask.order_id_));
}

// Replacing an unknown id is rejected.
TEST_F(Fixture, ReplaceUnknownRejects)
{
  book.replace(999, +10, UNCH);
  ASSERT_EQ(listener.replace_rejects.size(), 1u);
  EXPECT_EQ(listener.replace_rejects[0].id, 999u);
}

// new_price == PRICE_UNCHANGED keeps the current price while changing size.
TEST_F(Fixture, ReplacePriceUnchangedKeepsPrice)
{
  SimpleOrder bid(BUY, 10, 100);
  book.add(bid);

  book.replace(bid.order_id_, +25, UNCH);
  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_bid(10, 1, 125));       // still at price 10, qty 125
}

} // namespace babo::test
