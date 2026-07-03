// Ported from liquibook test/unit/changed_checker.h.
// Verifies the depth change-stamp bookkeeping that drives depth/bbo publication.
// Only change vs. the original: liquibook's book::ChangeId is a plain uint32_t here.
#pragma once

#include <book/depth.h>
#include <cstdint>
#include <iostream>

namespace babo::test {

using babo::book::Depth;
using babo::book::DepthLevel;
using ChangeId = std::uint32_t;

template <int SIZE = 5>
class ChangedChecker {
public:
  using SizedDepth = Depth<SIZE>;

  explicit ChangedChecker(SizedDepth& depth)
  : depth_(depth)
  {
    reset();
  }

  void reset() { last_change_ = depth_.last_change(); }

  bool verify_bid_changed(bool l0, bool l1, bool l2, bool l3, bool l4) {
    return verify_side_changed(depth_.bids(), l0, l1, l2, l3, l4);
  }
  bool verify_ask_changed(bool l0, bool l1, bool l2, bool l3, bool l4) {
    return verify_side_changed(depth_.asks(), l0, l1, l2, l3, l4);
  }

  bool verify_bid_stamps(ChangeId l0, ChangeId l1, ChangeId l2, ChangeId l3, ChangeId l4) {
    return verify_side_stamps(depth_.bids(), l0, l1, l2, l3, l4);
  }
  bool verify_ask_stamps(ChangeId l0, ChangeId l1, ChangeId l2, ChangeId l3, ChangeId l4) {
    return verify_side_stamps(depth_.asks(), l0, l1, l2, l3, l4);
  }

  bool verify_bbo_changed(bool bid_changed, bool ask_changed) {
    bool matched = true;
    if (depth_.bids()->changed_since(last_change_)) {
      if (!bid_changed) { std::cout << "best bid unexpected change" << std::endl; matched = false; }
    } else if (bid_changed) { std::cout << "best bid expected change" << std::endl; matched = false; }
    if (depth_.asks()->changed_since(last_change_)) {
      if (!ask_changed) { std::cout << "best ask unexpected change" << std::endl; matched = false; }
    } else if (ask_changed) { std::cout << "best ask expected change" << std::endl; matched = false; }
    return matched;
  }

  bool verify_bbo_stamps(ChangeId bid_stamp, ChangeId ask_stamp) {
    bool matched = true;
    if (depth_.bids()->last_change() != bid_stamp) {
      std::cout << "best bid change " << depth_.bids()->last_change() << std::endl; matched = false;
    }
    if (depth_.asks()->last_change() != ask_stamp) {
      std::cout << "best ask change " << depth_.asks()->last_change() << std::endl; matched = false;
    }
    return matched;
  }

private:
  ChangeId last_change_;
  SizedDepth& depth_;

  bool verify_level(const DepthLevel* levels, size_t index, bool expected) {
    if (levels[index].changed_since(last_change_) != expected) {
      std::cout << (expected ? "expected change level[" : "unexpected change level[")
                << index << "] " << levels[index].price() << std::endl;
      return false;
    }
    return true;
  }

  bool verify_side_changed(const DepthLevel* start, bool l0, bool l1, bool l2, bool l3, bool l4) {
    bool matched = true;
    matched = verify_level(start, 0, l0) && matched;
    matched = verify_level(start, 1, l1) && matched;
    matched = verify_level(start, 2, l2) && matched;
    matched = verify_level(start, 3, l3) && matched;
    matched = verify_level(start, 4, l4) && matched;
    return matched;
  }

  bool verify_side_stamps(const DepthLevel* start, ChangeId l0, ChangeId l1, ChangeId l2, ChangeId l3, ChangeId l4) {
    const ChangeId exp[5] = {l0, l1, l2, l3, l4};
    bool matched = true;
    for (int i = 0; i < 5; ++i) {
      if (start[i].last_change() != exp[i]) {
        std::cout << "change id[" << i << "] " << start[i].last_change() << std::endl;
        matched = false;
      }
    }
    return matched;
  }
};

} // namespace babo::test
