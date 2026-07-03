// Market-price behaviour, from liquibook ut_market_price.cpp.
//
// babo's market price is the last trade price (or a value set via
// set_market_price()); it gates stop triggering. NOTE a deliberate divergence
// from liquibook: babo does NOT rest an unfilled market order, so the liquibook
// "market-to-market once a price exists" scenarios (which rely on resting market
// orders) are expressed here in babo's terms -- unmatched market orders simply
// leave no residue.
#include "unit/babo_test_utils.h"

#include <gtest/gtest.h>

namespace babo::test {

using Fixture = BookFixture;

namespace {
constexpr bool BUY = true, SELL = false;
constexpr std::uint32_t PMKT = 0;
constexpr std::uint32_t prc = 9900;
constexpr std::uint32_t q100 = 100;
}

// With no prior trade and an empty book, crossing market orders find nothing and
// leave the market price unset. (In babo the unfilled market orders are discarded.)
TEST_F(Fixture, NoTradeLeavesMarketPriceUnset)
{
  SimpleOrder buy(BUY,  PMKT, q100);
  SimpleOrder sell(SELL, PMKT, q100);
  book.add(buy);
  book.add(sell);

  EXPECT_EQ(listener.fills.size(), 0u);
  EXPECT_EQ(book.market_price(), 0u);
  EXPECT_FALSE(resting(buy.order_id_));
  EXPECT_FALSE(resting(sell.order_id_));
}

// A trade sets the market price to the execution (maker) price.
TEST_F(Fixture, TradeSetsMarketPrice)
{
  SimpleOrder restBid(BUY, prc, q100);
  book.add(restBid);
  EXPECT_EQ(book.market_price(), 0u);         // resting alone doesn't set it

  SimpleOrder mktSell(SELL, PMKT, q100);       // hits the resting bid @9900
  book.add(mktSell);

  EXPECT_EQ(listener.filled_qty(mktSell.order_id_), q100);
  EXPECT_EQ(book.market_price(), prc);
}

// set_market_price() sets the reference price directly, with no trade.
TEST_F(Fixture, ExplicitlySettingMarketPrice)
{
  EXPECT_EQ(book.market_price(), 0u);
  book.set_market_price(prc);
  EXPECT_EQ(book.market_price(), prc);
  EXPECT_EQ(listener.fills.size(), 0u);
}

// A market order with a real book sweeps best-first at each maker's price and the
// last fill sets the market price.
TEST_F(Fixture, MarketOrderSweepsAtMakerPrices)
{
  SimpleOrder ask0(SELL, prc,       q100);     // 9900
  SimpleOrder ask1(SELL, prc + 100, q100);     // 10000
  book.add(ask0);
  book.add(ask1);

  SimpleOrder mktBuy(BUY, PMKT, 200);
  book.add(mktBuy);

  ASSERT_EQ(listener.fills.size(), 2u);
  EXPECT_EQ(listener.fills[0].cost, q100 * prc);
  EXPECT_EQ(listener.fills[1].cost, q100 * (prc + 100));
  EXPECT_EQ(book.market_price(), prc + 100);   // last trade price
  EXPECT_FALSE(resting(mktBuy.order_id_));
}

} // namespace babo::test
