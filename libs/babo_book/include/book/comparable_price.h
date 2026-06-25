//
// Created by Adminstudio on 6/25/2026.
//

#ifndef BABOMATCHINGENGINE_COMPARABLE_PRICE_H
#define BABOMATCHINGENGINE_COMPARABLE_PRICE_H

#include "types.h"
#include <compare>
#include <iostream>

namespace babo::book {

/// @brief A price that knows which side of the market it is on
/// Designed to be compared to other prices on the same side using
/// standard comparison operators (< > <= >= == !=) and to
/// compared to prices on the other side using the match() method.
///
/// Using the  '<' operation to sort a set of prices will result in the
/// prices being partially ordered from most liquid to least liquid.
/// i.e:
///   Market prices always sort first since they will match any counter price.
///   Sell side low prices sort before high prices because they match more buys.
///   Buy side high prices sort before low prices because they match more asks.
///
class ComparablePrice
{
  Price price_;
  bool buySide_;

public:
  /// @brief construct given side and price
  /// @param buySide controls whether price comparison is normal or reversed
  /// @param price is the price for this key, or 0 (MARKET_ORDER_PRICE) for market
  ComparablePrice(bool buySide, Price price)
    : price_(price)
    , buySide_(buySide)
  {
  }

  /// @brief Check possible trade
  /// Assumes rhs is on the opposite side
  bool matches(Price rhs) const
  {
    if(price_ == rhs)
    {
      return true;
    }
    if(buySide_)
    {
      return rhs < price_  || price_ == MARKET_ORDER_PRICE ;
    }
    return price_ < rhs || rhs == MARKET_ORDER_PRICE;
  }

  /// @brief order a key against a price on the same side.
  /// Side determines the sense of the comparison; the market price (0) is the most
  /// liquid and always orders first. weak_ordering because equivalent keys are not
  /// substitutable (equality ignores side).
  std::weak_ordering operator <=>(Price rhs) const
  {
    if(price_ == rhs)                return std::weak_ordering::equivalent; // includes market == market
    if(price_ == MARKET_ORDER_PRICE) return std::weak_ordering::less;       // market matches any counter -> first
    if(rhs    == MARKET_ORDER_PRICE) return std::weak_ordering::greater;
    if(buySide_)                     return rhs < price_ ? std::weak_ordering::less : std::weak_ordering::greater; // buys: highest first
    return price_ < rhs ? std::weak_ordering::less : std::weak_ordering::greater;                                 // sells: lowest first
  }

  /// @brief equality compare key to a price (without regard to side)
  bool operator ==(Price rhs) const
  {
    return price_ == rhs;
  }

  /// @brief order two keys, assuming they are on the same side.
  std::weak_ordering operator <=>(const ComparablePrice & rhs) const
  {
    return *this <=> rhs.price_;
  }

  /// @brief equality compare order map keys
  bool operator ==(const ComparablePrice & rhs) const
  {
    return *this == rhs.price_;
  }

  /// @brief access price.
  Price price() const
  {
    return price_;
  }

  /// @brief access side.
  bool isBuy() const
  {
    return buySide_;
  }

  /// @brief check to see if this is market price
  bool isMarket() const
  {
    return price_ == MARKET_ORDER_PRICE;
  }
};

// Reversed forms such as (Price < ComparablePrice) are synthesized by the compiler
// from the member operator<=> / operator== above, so no hand-written free operators are needed.

inline std::ostream & operator << (std::ostream & out, const ComparablePrice & key)
{
  out << (key.isBuy() ? "Buy at " : "Sell at ");
  if(key.isMarket())
  {
    out << "Market";
  }
  else
  {
    out << key.price();
  }
  return out;
}

}
#endif // BABOMATCHINGENGINE_COMPARABLE_PRICE_H
