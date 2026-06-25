// --- babo_book (PLACEHOLDER) ------------------------------------------------
// Stub implementation so everything links. These do nothing yet -- the bench
// numbers for babo are meaningless until you implement real matching here.
//
// Day-1 plan (from building_diary.md): start from liquibook's two-multimap
// design, then try the cache-aware order-book layout from arxiv 2606.01183.
#include "babo/order_book.hpp"

namespace babo {

OrderBook::OrderBook()  = default;
OrderBook::~OrderBook() = default;

void OrderBook::add(OrderId /*id*/, bool /*is_buy*/, Price /*price*/, Quantity /*qty*/) {
    // TODO: insert into the book and match against the opposite side.
}

void OrderBook::cancel(OrderId /*id*/) {
    // TODO: remove the resting order.
}

std::size_t OrderBook::resting_count() const {
    // TODO: return bids + asks still on the book.
    return 0;
}

} // namespace babo
