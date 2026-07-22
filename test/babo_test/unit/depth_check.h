// Ported from liquibook test/unit/depth_check.h.
// Walks the aggregate Depth<SIZE> and asserts each level's price/count/qty.
// babo's Depth / DepthLevel API matches liquibook's, so this is a near-verbatim
// port: liquibook's Price/Quantity typedefs become plain uint32_t, and the
// helpers return bool (compose with EXPECT_TRUE at the call site).
#pragma once

#include <book/depth.h>
#include <cstdint>
#include <iostream>

namespace babo::test {

using babo::book::DepthLevel;

// Templated on the book type so `Book::DepthSnapshot` resolves to Depth<SIZE>.
template <typename Book>
class DepthCheck {
  using SizedDepth = typename Book::DepthSnapshot;

public:
  explicit DepthCheck(const SizedDepth& depth)
  : depth_(depth)
  {
    reset();
  }

  static bool verify_depth(const DepthLevel& level,
                           uint32_t price,
                           uint32_t count,
                           uint32_t qty)
  {
    bool matched = true;
    if (level.price() != price) {
      std::cout << "Price " << level.price() << " expecting " << price << std::endl;
      matched = false;
    }
    if (level.order_count() != count) {
      std::cout << "Level: " << level.price() << " Count " << level.order_count()
                << " expecting " << count << std::endl;
      matched = false;
    }
    if (level.aggregate_qty() != qty) {
      std::cout << "Level: " << level.price() << " Quantity " << level.aggregate_qty()
                << " expecting " << qty << std::endl;
      matched = false;
    }
    if (level.is_excess()) {
      std::cout << "Marked as excess" << std::endl;
      matched = false;
    }
    return matched;
  }

  bool verify_bid(uint32_t price, uint32_t count, uint32_t qty)
  {
    return verify_depth(*next_bid_++, price, count, qty);
  }

  bool verify_ask(uint32_t price, uint32_t count, uint32_t qty)
  {
    return verify_depth(*next_ask_++, price, count, qty);
  }

  // Assert every remaining bid level (from where verify_bid left off) is empty.
  bool verify_bids_done()
  {
    while (next_bid_ != depth_.last_bid_level() + 1) {
      if (next_bid_->order_count() != 0) return false;
      ++next_bid_;
    }
    return true;
  }

  // Assert every remaining ask level is empty.
  bool verify_asks_done()
  {
    while (next_ask_ != depth_.last_ask_level() + 1) {
      if (next_ask_->order_count() != 0) return false;
      ++next_ask_;
    }
    return true;
  }

  void reset()
  {
    next_bid_ = depth_.bids();
    next_ask_ = depth_.asks();
  }

private:
  const SizedDepth& depth_;
  const DepthLevel* next_bid_;
  const DepthLevel* next_ask_;
};

} // namespace babo::test
