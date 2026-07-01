//
// Created by Adminstudio on 6/25/2026.
//

#ifndef BABOMATCHINGENGINE_TYPES_H
#define BABOMATCHINGENGINE_TYPES_H

#include <cstdint>

namespace babo::book {
using  OrderConditions = uint32_t;

enum class OrderCondition : uint32_t {
  oc_no_conditions = 0,
  oc_all_or_none = 1,
  oc_immediate_or_cancel = oc_all_or_none << 1,
  oc_fill_or_kill = oc_all_or_none | oc_immediate_or_cancel,
  oc_stop = oc_immediate_or_cancel << 1
};

inline constexpr uint32_t MARKET_ORDER_PRICE{0};
inline constexpr uint32_t PRICE_UNCHANGED{0};
inline constexpr int32_t SIZE_UNCHANGED{0};

}
#endif // BABOMATCHINGENGINE_TYPES_H
