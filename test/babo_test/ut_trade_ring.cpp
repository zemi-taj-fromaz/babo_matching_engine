// The inline SPSC trade ring: FIFO drain, and oldest-dropped overflow when the
// consumer falls more than TRADE_CAP behind.
#include <book/matching_book.h>
#include <simple/simple_order.h>

#include <gtest/gtest.h>

#include <vector>

namespace babo::test {

using babo::simple::SimpleOrder;

// Trades drain oldest-first with the right maker/taker/qty/price.
TEST(TradeRing, DrainsFifo)
{
  SimpleOrder::reset_id_generator();
  babo::book::matching_book<5> book;

  SimpleOrder ask(false, 10, 300);
  const auto ask_id = ask.order_id_;
  book.add(ask);

  std::vector<std::uint32_t> takers;
  for (int i = 0; i < 3; ++i) { SimpleOrder buy(true, 10, 100); takers.push_back(buy.order_id_); book.add(buy); }

  ASSERT_EQ(book.trades_available(), 3u);
  for (int i = 0; i < 3; ++i) {
    const auto& t = book.read_trade();
    EXPECT_EQ(t.maker_id, ask_id);
    EXPECT_EQ(t.taker_id, takers[i]);
    EXPECT_EQ(t.qty, 100u);
    EXPECT_EQ(t.price, 10u);
  }
  EXPECT_EQ(book.trades_available(), 0u);
}

// With TRADE_CAP = 4 and 6 undrained trades, only the most recent 4 survive.
TEST(TradeRing, OverflowDropsOldest)
{
  SimpleOrder::reset_id_generator();
  babo::book::matching_book<5, 4> book;   // TRADE_CAP = 4

  SimpleOrder ask(false, 10, 1000);
  book.add(ask);

  std::vector<std::uint32_t> takers;
  for (int i = 0; i < 6; ++i) { SimpleOrder buy(true, 10, 10); takers.push_back(buy.order_id_); book.add(buy); }

  // 6 trades executed, cap is 4 -> the two oldest were overwritten.
  EXPECT_EQ(book.trades_available(), 4u);

  // Retained trades are the last four takers (indices 2..5), still in order.
  for (int i = 2; i < 6; ++i) {
    ASSERT_TRUE(book.trades_available());
    const auto& t = book.read_trade();
    EXPECT_EQ(t.taker_id, takers[i]);
    EXPECT_EQ(t.qty, 10u);
    EXPECT_EQ(t.price, 10u);
  }
  EXPECT_EQ(book.trades_available(), 0u);
}

} // namespace babo::test
