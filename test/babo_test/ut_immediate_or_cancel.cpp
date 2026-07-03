// IOC and FOK (fill-or-kill = AON|IOC) matching, translated from liquibook
// ut_immediate_or_cancel.cpp. Both conditions discard any unfilled remainder
// instead of resting it -- babo does this silently (no on_cancel), so we assert
// via fills + depth + resting() rather than order state.
#include "unit/babo_test_utils.h"

#include <gtest/gtest.h>

namespace babo::test {

using Fixture = BookFixture;

constexpr babo::book::OrderConditions IOC = babo::book::oc_immediate_or_cancel;
constexpr babo::book::OrderConditions FOK = babo::book::oc_fill_or_kill;

// ------------------------------------------------------------------ IOC bid

// IOC that crosses nothing: no fills, nothing rests.
TEST_F(Fixture, IocBidNoMatch)
{
  SimpleOrder ask1(false, 1251, 100);
  SimpleOrder ask2(false, 1252, 100);
  SimpleOrder bid1(true,  1249, 100);
  SimpleOrder bid2(true,  1248, 100);
  book.add(ask1); book.add(ask2); book.add(bid1); book.add(bid2);

  SimpleOrder bid0(true, 1250, 100, 0, IOC);   // below best ask -> no cross
  const auto bid0_id = bid0.order_id_;
  book.add(bid0);

  EXPECT_EQ(listener.fills.size(), 0u);
  EXPECT_FALSE(resting(bid0_id));

  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_bid(1249, 1, 100));
  EXPECT_TRUE(dc.verify_bid(1248, 1, 100));
  EXPECT_TRUE(dc.verify_ask(1251, 1, 100));
  EXPECT_TRUE(dc.verify_ask(1252, 1, 100));
}

// IOC partially fills at the touch, discards the rest.
TEST_F(Fixture, IocBidPartialMatch)
{
  SimpleOrder ask0(false, 1250, 100);
  SimpleOrder ask1(false, 1251, 100);
  SimpleOrder ask2(false, 1252, 100);
  SimpleOrder bid1(true,  1249, 100);
  SimpleOrder bid2(true,  1248, 100);
  book.add(ask0); book.add(ask1); book.add(ask2); book.add(bid1); book.add(bid2);

  SimpleOrder bid0(true, 1250, 300, 0, IOC);   // only ask0 @1250 crosses
  const auto bid0_id = bid0.order_id_;
  book.add(bid0);

  EXPECT_EQ(listener.filled_qty(bid0_id), 100u);
  EXPECT_FALSE(resting(bid0_id));              // 200 remainder discarded
  EXPECT_EQ(book.market_price(), 1250u);

  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_bid(1249, 1, 100));
  EXPECT_TRUE(dc.verify_bid(1248, 1, 100));
  EXPECT_TRUE(dc.verify_ask(1251, 1, 100));    // ask0 gone
  EXPECT_TRUE(dc.verify_ask(1252, 1, 100));
}

// IOC fully filled by a single deeper level.
TEST_F(Fixture, IocBidFullMatch)
{
  SimpleOrder ask0(false, 1250, 400);
  SimpleOrder ask1(false, 1251, 100);
  book.add(ask0); book.add(ask1);

  SimpleOrder bid0(true, 1250, 300, 0, IOC);
  const auto bid0_id = bid0.order_id_;
  book.add(bid0);

  EXPECT_EQ(listener.filled_qty(bid0_id), 300u);
  EXPECT_FALSE(resting(bid0_id));

  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_ask(1250, 1, 100));    // 400 - 300
  EXPECT_TRUE(dc.verify_ask(1251, 1, 100));
}

// IOC sweeps two ask levels; executes each at the maker's price.
TEST_F(Fixture, IocBidMultiMatch)
{
  SimpleOrder ask0(false, 1250, 400);
  SimpleOrder ask1(false, 1251, 100);
  SimpleOrder ask2(false, 1252, 100);
  book.add(ask0); book.add(ask1); book.add(ask2);

  SimpleOrder bid0(true, 1251, 500, 0, IOC);
  const auto bid0_id = bid0.order_id_;
  book.add(bid0);

  EXPECT_EQ(listener.filled_qty(bid0_id), 500u);
  ASSERT_EQ(listener.fills.size(), 2u);
  EXPECT_EQ(listener.fills[0].qty, 400u);      // @1250
  EXPECT_EQ(listener.fills[1].qty, 100u);      // @1251
  EXPECT_FALSE(resting(bid0_id));
  EXPECT_EQ(book.market_price(), 1251u);

  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_ask(1252, 1, 100));
  EXPECT_TRUE(dc.verify_asks_done());
}

// ------------------------------------------------------------------ FOK bid

// FOK that can't be fully filled trades nothing at all.
TEST_F(Fixture, FokBidPartialMatchKills)
{
  SimpleOrder ask0(false, 1250, 100);
  SimpleOrder ask1(false, 1251, 100);
  book.add(ask0); book.add(ask1);

  SimpleOrder bid0(true, 1250, 300, 0, FOK);   // needs 300, only 100 crosses
  const auto bid0_id = bid0.order_id_;
  book.add(bid0);

  EXPECT_EQ(listener.fills.size(), 0u);        // killed, no partial
  EXPECT_FALSE(resting(bid0_id));

  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_ask(1250, 1, 100));    // untouched
  EXPECT_TRUE(dc.verify_ask(1251, 1, 100));
}

// FOK that can be fully filled executes completely, rests nothing.
TEST_F(Fixture, FokBidFullMatch)
{
  SimpleOrder ask0(false, 1250, 400);
  book.add(ask0);

  SimpleOrder bid0(true, 1250, 300, 0, FOK);
  const auto bid0_id = bid0.order_id_;
  book.add(bid0);

  EXPECT_EQ(listener.filled_qty(bid0_id), 300u);
  EXPECT_FALSE(resting(bid0_id));

  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_ask(1250, 1, 100));
}

// FOK filled across multiple levels.
TEST_F(Fixture, FokBidMultiMatch)
{
  SimpleOrder ask0(false, 1250, 400);
  SimpleOrder ask1(false, 1251, 100);
  book.add(ask0); book.add(ask1);

  SimpleOrder bid0(true, 1251, 500, 0, FOK);
  const auto bid0_id = bid0.order_id_;
  book.add(bid0);

  EXPECT_EQ(listener.filled_qty(bid0_id), 500u);
  EXPECT_FALSE(resting(bid0_id));
  EXPECT_TRUE(book.bids().find_order(bid0_id) == nullptr);
  EXPECT_TRUE(book.asks().find_order(ask0.order_id_) == nullptr);
}

// ------------------------------------------------------------------ IOC ask

// Symmetric IOC on the sell side: partial fill, remainder discarded.
TEST_F(Fixture, IocAskPartialMatch)
{
  SimpleOrder bid0(true,  1250, 100);
  SimpleOrder bid1(true,  1249, 100);
  SimpleOrder bid2(true,  1248, 100);
  book.add(bid0); book.add(bid1); book.add(bid2);

  SimpleOrder ask0(false, 1250, 300, 0, IOC);  // only bid0 @1250 crosses
  const auto ask0_id = ask0.order_id_;
  book.add(ask0);

  EXPECT_EQ(listener.filled_qty(ask0_id), 100u);
  EXPECT_FALSE(resting(ask0_id));
  EXPECT_EQ(book.market_price(), 1250u);

  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_bid(1249, 1, 100));
  EXPECT_TRUE(dc.verify_bid(1248, 1, 100));
}

// IOC sell sweeping two bid levels.
TEST_F(Fixture, IocAskMultiMatch)
{
  SimpleOrder bid0(true, 1250, 300);
  SimpleOrder bid1(true, 1249, 100);
  SimpleOrder bid2(true, 1248, 100);
  book.add(bid0); book.add(bid1); book.add(bid2);

  SimpleOrder ask0(false, 1249, 400, 0, IOC);
  const auto ask0_id = ask0.order_id_;
  book.add(ask0);

  EXPECT_EQ(listener.filled_qty(ask0_id), 400u);
  ASSERT_EQ(listener.fills.size(), 2u);
  EXPECT_EQ(listener.fills[0].qty, 300u);      // @1250 (best bid first)
  EXPECT_EQ(listener.fills[1].qty, 100u);      // @1249
  EXPECT_FALSE(resting(ask0_id));

  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_bid(1248, 1, 100));
  EXPECT_TRUE(dc.verify_bids_done());
}

// FOK sell that cannot fully fill kills.
TEST_F(Fixture, FokAskPartialMatchKills)
{
  SimpleOrder bid0(true, 1250, 100);
  SimpleOrder bid1(true, 1249, 100);
  book.add(bid0); book.add(bid1);

  SimpleOrder ask0(false, 1250, 300, 0, FOK);
  const auto ask0_id = ask0.order_id_;
  book.add(ask0);

  EXPECT_EQ(listener.fills.size(), 0u);
  EXPECT_FALSE(resting(ask0_id));

  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_bid(1250, 1, 100));
  EXPECT_TRUE(dc.verify_bid(1249, 1, 100));
}

} // namespace babo::test
