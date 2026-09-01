#pragma once

#include <variant>

#include <lobcore/types.hpp>

namespace lobcore {

struct AddLimit {
  OrderId   id;
  Side      side;
  Price     price;
  Qty       qty;
  Timestamp decided_at = 0;
};

struct CancelOrder {
  OrderId id;
};

using OrderMessage = std::variant<AddLimit, CancelOrder>;

}  // namespace lobcore
