#include <lazerbook/reconstruct.hpp>

#include <variant>

namespace lazerbook {

Reconstructor::Reconstructor(Book& book, OrderPool& pool) : book_(book), pool_(pool) {}

void Reconstructor::add_order(OrderId ref, Side side, Price4 price, std::uint32_t shares) {
    if (!book_.in_range(price)) {
        ++stats_.skip_oor;
        return;
    }
    Order* o = pool_.acquire();
    if (o == nullptr) {
        ++stats_.skip_pool_exhausted;
        return;
    }
    o->id = ref;
    o->side = side;
    o->price = price;
    o->shares = shares;
    book_.add(o);
    orders_.emplace(value_of(ref), o);
}

void Reconstructor::reduce_order(OrderId ref, std::uint32_t qty) {
    auto it = orders_.find(value_of(ref));
    if (it == orders_.end()) {
        ++stats_.skip_unknown_ref;
        return;
    }
    Order* o = it->second;
    if (qty >= o->shares) {
        book_.reduce(o, o->shares);  // unlinks at zero
        pool_.release(o);
        orders_.erase(it);
    } else {
        book_.reduce(o, qty);
    }
}

void Reconstructor::delete_order(OrderId ref) {
    auto it = orders_.find(value_of(ref));
    if (it == orders_.end()) {
        ++stats_.skip_unknown_ref;
        return;
    }
    book_.remove(it->second);
    pool_.release(it->second);
    orders_.erase(it);
}

void Reconstructor::apply(itch::Message const& msg) {
    std::visit(
        [this](auto const& m) {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, itch::AddOrder> ||
                          std::is_same_v<T, itch::AddOrderMpid>) {
                add_order(m.order_reference_number, m.buy_sell_indicator, m.price, m.shares);
                ++stats_.added;
            } else if constexpr (std::is_same_v<T, itch::OrderExecuted>) {
                reduce_order(m.order_reference_number, m.executed_shares);
                ++stats_.executed;
            } else if constexpr (std::is_same_v<T, itch::OrderExecutedWithPrice>) {
                reduce_order(m.order_reference_number, m.executed_shares);
                ++stats_.executed;
            } else if constexpr (std::is_same_v<T, itch::OrderCancel>) {
                reduce_order(m.order_reference_number, m.cancelled_shares);
                ++stats_.cancelled;
            } else if constexpr (std::is_same_v<T, itch::OrderDelete>) {
                delete_order(m.order_reference_number);
                ++stats_.deleted;
            } else if constexpr (std::is_same_v<T, itch::OrderReplace>) {
                auto it = orders_.find(value_of(m.original_order_reference_number));
                if (it == orders_.end()) {
                    ++stats_.skip_unknown_ref;
                } else {
                    Side const side = it->second->side;
                    book_.remove(it->second);
                    pool_.release(it->second);
                    orders_.erase(it);
                    add_order(m.new_order_reference_number, side, m.price, m.shares);
                }
                ++stats_.replaced;
            } else if constexpr (std::is_same_v<T, itch::Trade> ||
                                 std::is_same_v<T, itch::CrossTrade> ||
                                 std::is_same_v<T, itch::BrokenTrade>) {
                ++stats_.trades;
            } else {
                ++stats_.skip_unhandled;
            }
        },
        msg
    );
}

void Reconstructor::apply_bytes(std::span<std::uint8_t const> bytes) {
    auto res = itch::parse(bytes);
    if (!res.has_value()) {
        ++stats_.skip_parse_error;
        return;
    }
    apply(*res);
}

}  // namespace lazerbook
