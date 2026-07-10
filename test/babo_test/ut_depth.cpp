// Aggregate depth, DERIVED from the tree on demand (matching_book::depth()).
//
// Exercises price ordering, per-level aggregation, and the "spill/restore" behaviour
// with more distinct prices than SIZE -- which now falls out for free from walking the
// tree's top-SIZE levels (drop the best, the next level is simply the next node in the
// walk; no excess map to restore from).
#include "unit/babo_test_utils.h"

#include <gtest/gtest.h>

namespace babo::test {

using Fixture   = BookFixture;      // matching_book<5>
using SmallBook = BookFixtureT<3>;  // matching_book<3> -- easy to overflow SIZE

namespace { constexpr bool BUY = true, SELL = false; }

// Bids are ordered highest-first regardless of insertion order.
TEST_F(Fixture, BidsSortedBestFirst)
{
  for (std::uint32_t p : {1250u, 1255u, 1240u, 1245u}) { SimpleOrder o(BUY, p, 100); book.add(o); }
  const auto* b = book.depth().bids();
  EXPECT_EQ(b[0].price(), 1255u);
  EXPECT_EQ(b[1].price(), 1250u);
  EXPECT_EQ(b[2].price(), 1245u);
  EXPECT_EQ(b[3].price(), 1240u);
}

// Asks are ordered lowest-first.
TEST_F(Fixture, AsksSortedBestFirst)
{
  for (std::uint32_t p : {3250u, 3230u, 3245u, 3235u}) { SimpleOrder o(SELL, p, 100); book.add(o); }
  const auto* a = book.depth().asks();
  EXPECT_EQ(a[0].price(), 3230u);
  EXPECT_EQ(a[1].price(), 3235u);
  EXPECT_EQ(a[2].price(), 3245u);
  EXPECT_EQ(a[3].price(), 3250u);
}

// Multiple orders at one price aggregate into a single level (count + qty).
TEST_F(Fixture, AggregatePerLevel)
{
  SimpleOrder o1(BUY, 100, 30), o2(BUY, 100, 70), o3(BUY, 99, 40);
  book.add(o1); book.add(o2); book.add(o3);

  DepthCheck<Book> dc(book.depth());
  EXPECT_TRUE(dc.verify_bid(100, 2, 100));
  EXPECT_TRUE(dc.verify_bid(99,  1, 40));
}

// With more distinct prices than SIZE, the worst spill to the excess map; erasing
// a visible level restores the best excess into view.
TEST_F(SmallBook, ExcessLevelsSpillAndRestore)
{
  // Five distinct bid prices into a size-3 depth.
  SimpleOrder o10(BUY, 10, 100); book.add(o10);
  SimpleOrder o12(BUY, 12, 100); book.add(o12);
  SimpleOrder o14(BUY, 14, 100); book.add(o14);   // current best
  SimpleOrder o11(BUY, 11, 100); book.add(o11);
  SimpleOrder o13(BUY, 13, 100); book.add(o13);

  const auto* b = book.depth().bids();
  EXPECT_EQ(b[0].price(), 14u);   // visible: 14, 13, 12
  EXPECT_EQ(b[1].price(), 13u);
  EXPECT_EQ(b[2].price(), 12u);   // 11 and 10 are in the excess map

  book.cancel(o14.order_id_);      // drop the best -> excess best (11) restores

  b = book.depth().bids();
  EXPECT_EQ(b[0].price(), 13u);
  EXPECT_EQ(b[1].price(), 12u);
  EXPECT_EQ(b[2].price(), 11u);
}

} // namespace babo::test
