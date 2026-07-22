#include <book/depth_constants.h>
#include <book/matching_book.h>
#include <book/order_listener.h>
#include <book/simple_order.h>

#include <iostream>
#include <string_view>

namespace {

using Book = babo::book::matching_book<5>;
using Order = babo::simple::SimpleOrder;

class ConsoleListener final : public Book::OrderEventListener {
public:
    void on_accept(const std::uint32_t& id) override
    {
        std::cout << "accept  id=" << id << '\n';
    }

    void on_reject(const std::uint32_t& id, const char* reason) override
    {
        std::cout << "reject  id=" << id << " reason=" << reason << '\n';
    }

    void on_fill(const std::uint32_t& taker, const std::uint32_t& maker,
                 std::uint32_t quantity, std::uint32_t cost) override
    {
        const std::uint32_t price = quantity == 0 ? 0 : cost / quantity;
        std::cout << "fill    maker=" << maker << " taker=" << taker
                  << " qty=" << quantity << " price=" << price << '\n';
    }

    void on_cancel(const std::uint32_t& id) override
    {
        std::cout << "cancel  id=" << id << '\n';
    }

    void on_cancel_reject(const std::uint32_t& id, const char* reason) override
    {
        std::cout << "cancel-reject id=" << id << " reason=" << reason << '\n';
    }

    void on_replace(const std::uint32_t& id, const std::int32_t& quantity_delta,
                    std::uint32_t price) override
    {
        std::cout << "replace id=" << id << " delta=" << quantity_delta
                  << " price=" << price << '\n';
    }

    void on_replace_reject(const std::uint32_t& id, const char* reason) override
    {
        std::cout << "replace-reject id=" << id << " reason=" << reason << '\n';
    }
};

void print_side(std::string_view name, const babo::book::DepthLevel* levels,
                std::size_t count)
{
    std::cout << name << ':';
    bool any = false;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (levels[i].price() == babo::book::INVALID_LEVEL_PRICE) break;
        std::cout << ' ' << levels[i].price() << '@' << levels[i].aggregate_qty()
                  << '(' << levels[i].order_count() << ')';
        any = true;
    }
    if (!any) std::cout << " empty";
    std::cout << '\n';
}

void print_depth(Book& book)
{
    const auto& depth = book.depth();
    print_side("bids", depth.bids(), 5);
    print_side("asks", depth.asks(), 5);
}

} // namespace

int main()
{
    Book book;
    ConsoleListener listener;
    book.set_order_listener(&listener);

    Order bid(/*is_buy=*/true, 100, 10);
    Order ask(/*is_buy=*/false, 105, 7);
    const std::uint32_t ask_id = ask.order_id_;

    book.add(bid);
    book.add(ask);

    std::cout << "\nafter resting orders\n";
    print_depth(book);

    // Crosses the resting bid. Trades execute at the maker's price (100),
    // leaving six units of the original bid on the book.
    Order crossing_sell(/*is_buy=*/false, 100, 4);
    book.add(crossing_sell);

    std::cout << "\nafter match\n";
    print_depth(book);

    book.cancel(ask_id);

    std::cout << "\nafter cancel\n";
    print_depth(book);
}
