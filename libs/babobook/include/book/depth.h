#ifndef BABOMATCHINGENGINE_DEPTH_H
#define BABOMATCHINGENGINE_DEPTH_H

#include "depth_constants.h"
#include "depth_level.h"

namespace babo::book {

/// @brief Aggregate top-of-book view: SIZE price levels per side, bids then asks.
///
/// This is a passive SNAPSHOT. matching_book rebuilds it on demand
template <int SIZE = 5>
class Depth {
public:
  Depth() { clear(); }

  /// @brief reset every level to empty; call before a rebuild
  void clear() {
    for (DepthLevel& l : levels_) l.set(INVALID_LEVEL_PRICE, 0, 0);
  }

  const DepthLevel* bids()           const { return levels_; }
  const DepthLevel* asks()           const { return levels_ + SIZE; }
  const DepthLevel* last_bid_level() const { return levels_ + (SIZE - 1); }
  const DepthLevel* last_ask_level() const { return levels_ + (SIZE * 2 - 1); }
  const DepthLevel* end()            const { return levels_ + (SIZE * 2); }

  DepthLevel* bids()           { return levels_; }
  DepthLevel* asks()           { return levels_ + SIZE; }
  DepthLevel* last_bid_level() { return levels_ + (SIZE - 1); }
  DepthLevel* last_ask_level() { return levels_ + (SIZE * 2 - 1); }

private:
  // [0, SIZE)      bids, best (highest) first
  // [SIZE, 2*SIZE) asks, best (lowest)  first
  DepthLevel levels_[SIZE * 2];
};

}

#endif // BABOMATCHINGENGINE_DEPTH_H
