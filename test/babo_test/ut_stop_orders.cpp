// Stop-order parking and triggering, from liquibook ut_stop_orders.cpp.
//
// babo parks a stop while its trigger is unreached (buy: market < stop; sell:
// market > stop) and fires it when the market -- moved by a trade or by
// set_market_price() -- reaches it. Parked stops live outside the visible depth,
// so resting() is false for them until they fire. (liquibook's "trade immediately"
// case relied on resting market orders, which babo discards, so it's replaced here
// by trigger-focused babo-native cases.)
#include "unit/babo_test_utils.h"

#include <gtest/gtest.h>

namespace babo::test {

using Fixture = BookFixture;

namespace {
constexpr bool BUY = true, SELL = false;
constexpr std::uint32_t PMKT = 0;
constexpr std::uint32_t p53 = 53, p54 = 54, p55 = 55, p56 = 56, p57 = 57;
constexpr std::uint32_t q100 = 100, q1000 = 1000;
constexpr babo::book::OrderConditions AON = babo::book::oc_all_or_none;
}

// Stops whose triggers sit off-market are accepted, parked, and do not trade.
TEST_F(Fixture, StopOrdersOffMarketNoTrade)
{
  SimpleOrder seedBid(BUY, p55, q100);
  SimpleOrder seedAsk(SELL, PMKT, q100);      // market sell hits the bid -> price 55
  book.add(seedBid);
  book.add(seedAsk);
  ASSERT_EQ(book.market_price(), p55);

  SimpleOrder buyStop(BUY,  PMKT, q100, p56);  // triggers at >=56; market is 55
  SimpleOrder sellStop(SELL, PMKT, q100, p54); // triggers at <=54; market is 55
  const auto buy_id = buyStop.order_id_;
  const auto sell_id = sellStop.order_id_;
  listener.clear();
  book.add(buyStop);
  book.add(sellStop);

  EXPECT_TRUE(listener.was_accepted(buy_id));
  EXPECT_TRUE(listener.was_accepted(sell_id));
  EXPECT_EQ(listener.fills.size(), 0u);
  EXPECT_FALSE(resting(buy_id));   // parked, not in visible depth
  EXPECT_FALSE(resting(sell_id));
  EXPECT_EQ(book.market_price(), p55);
}

// A stop whose trigger is already satisfied when added goes live immediately.
TEST_F(Fixture, StopAlreadyTriggeredSubmitsImmediately)
{
  book.set_market_price(p55);
  SimpleOrder restAsk(SELL, p55, q100);
  book.add(restAsk);

  // Buy stop @55: market (55) is NOT below the stop, so it isn't parked -- it
  // submits at once and crosses the resting ask.
  SimpleOrder buyStop(BUY, p55, q100, p55);
  book.add(buyStop);

  EXPECT_EQ(listener.filled_qty(buyStop.order_id_), q100);
  EXPECT_FALSE(resting(restAsk.order_id_));
}

// set_market_price() alone can reach a parked stop and fire it.
TEST_F(Fixture, SetMarketPriceTriggersStop)
{
  book.set_market_price(p55);
  SimpleOrder restAsk(SELL, p57, q100);
  book.add(restAsk);

  SimpleOrder buyStop(BUY, p57, q100, p56);   // parked: market 55 < stop 56
  const auto stop_id = buyStop.order_id_;
  book.add(buyStop);
  EXPECT_FALSE(resting(stop_id));
  EXPECT_EQ(listener.fills.size(), 0u);

  book.set_market_price(p56);                 // reaches the stop -> fires -> trades @57
  EXPECT_EQ(listener.filled_qty(stop_id), q100);
  EXPECT_FALSE(resting(restAsk.order_id_));
}

// A parked stop cancelled before its trigger never fires.
TEST_F(Fixture, CancelParkedStop)
{
  book.set_market_price(p55);
  SimpleOrder restAsk(SELL, p57, q100);
  book.add(restAsk);

  SimpleOrder buyStop(BUY, p57, q100, p56);
  const auto stop_id = buyStop.order_id_;
  book.add(buyStop);

  book.cancel(stop_id);
  EXPECT_TRUE(listener.was_cancelled(stop_id));

  book.set_market_price(p56);                 // would have triggered -- but it's gone
  EXPECT_EQ(listener.fills.size(), 0u);
}

// Trades that move the market price cascade into parked stops (liquibook's
// TestStopMarketOrdersTradeWhenStopPriceReached, translated verbatim in spirit).
TEST_F(Fixture, StopsTriggeredByMarketMove)
{
  SimpleOrder order0(BUY,  p53, q100);
  SimpleOrder order1(SELL, p57, q100);
  book.set_market_price(p55);
  book.add(order0);
  book.add(order1);
  ASSERT_EQ(listener.fills.size(), 0u);       // 53 vs 57, no cross

  SimpleOrder buyStop(BUY,  PMKT, q100, p56);  // parks (55 < 56)
  SimpleOrder sellStop(SELL, PMKT, q100, p54); // parks (55 > 54)
  book.add(buyStop);
  book.add(sellStop);
  ASSERT_EQ(listener.fills.size(), 0u);

  // A 1000-lot trade at 56 moves the market up and should trigger buyStop, which
  // then trades with order1 at order1's price (57).
  SimpleOrder o4(BUY,  p56, q1000, 0, AON);
  SimpleOrder o5(SELL, p56, q1000, 0, AON);
  book.add(o4);
  book.add(o5);
  EXPECT_EQ(book.market_price(), p57);
  EXPECT_EQ(listener.filled_qty(order1.order_id_), q100);   // triggered fill @57
  EXPECT_EQ(listener.filled_qty(buyStop.order_id_), q100);

  // A 1000-lot trade at 54 moves the market down and triggers sellStop, which
  // trades with order0 at order0's price (53).
  SimpleOrder o6(BUY,  p54, q1000, 0, AON);
  SimpleOrder o7(SELL, p54, q1000, 0, AON);
  book.add(o6);
  book.add(o7);
  EXPECT_EQ(book.market_price(), p53);
  EXPECT_EQ(listener.filled_qty(order0.order_id_), q100);
  EXPECT_EQ(listener.filled_qty(sellStop.order_id_), q100);
}

} // namespace babo::test
