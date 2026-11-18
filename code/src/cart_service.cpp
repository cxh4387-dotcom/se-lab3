// Copyright 2026 251220048 Cao Xuhang. All rights reserved.

#include <string>
#include <vector>

#include "delivery/services.h"

namespace delivery {

CartService::CartService(const Repositories& repos, Clock* clock)
    : repos_(repos), clock_(clock) {}

void CartService::AddItem(const User& customer, DishId dish_id, int quantity) {
  Check(customer.role == Role::kCustomer, ErrorCode::kPermissionDenied,
        "只有顾客可以使用购物车");
  domain::ValidateQuantity(quantity);
  const auto dish = repos_.dishes.FindById(dish_id);
  Check(dish.has_value(), ErrorCode::kNotFound, "菜品不存在");
  Check(dish->status == DishStatus::kOnSale, ErrorCode::kOutOfStock,
        "该菜品当前不可购买");
  const auto shop = repos_.shops.FindById(dish->shop_id);
  Check(shop.has_value() && shop->AcceptsNewOrder(clock_->Now()),
        ErrorCode::kConflict, "店铺当前不接单");
  const auto stock = repos_.stocks.Find(dish_id, ToDateKey(clock_->Now()));
  const int remaining = stock.has_value() ? stock->Remaining() : 0;
  Check(remaining >= quantity, ErrorCode::kOutOfStock, "库存不足");

  int merged = quantity;
  Money snapshot = dish->price;
  for (const CartItem& item : repos_.carts.ListByCustomer(customer.id)) {
    if (item.dish_id == dish_id && item.spec == dish->spec) {
      merged = item.quantity + quantity;
      snapshot = item.price_when_added;
      break;
    }
  }
  // The merged quantity, not only the newly added one, must fit the stock.
  Check(remaining >= merged, ErrorCode::kOutOfStock, "库存不足");
  Check(merged <= 99, ErrorCode::kInvalidArgument, "单个菜品最多购买 99 份");

  CartItem item;
  item.customer_id = customer.id;
  item.dish_id = dish_id;
  item.spec = dish->spec;
  item.quantity = merged;
  item.price_when_added = snapshot;
  repos_.carts.Upsert(item);
}

void CartService::UpdateQuantity(const User& customer, DishId dish_id,
                                 const std::string& spec, int quantity) {
  Check(customer.role == Role::kCustomer, ErrorCode::kPermissionDenied,
        "只有顾客可以使用购物车");
  if (quantity <= 0) {
    RemoveItem(customer, dish_id, spec);
    return;
  }
  domain::ValidateQuantity(quantity);
  Money snapshot = 0;
  bool found = false;
  for (const CartItem& item : repos_.carts.ListByCustomer(customer.id)) {
    if (item.dish_id == dish_id && item.spec == spec) {
      snapshot = item.price_when_added;
      found = true;
      break;
    }
  }
  Check(found, ErrorCode::kNotFound, "购物车中没有该菜品");
  CartItem item;
  item.customer_id = customer.id;
  item.dish_id = dish_id;
  item.spec = spec;
  item.quantity = quantity;
  item.price_when_added = snapshot;
  repos_.carts.Upsert(item);
}

void CartService::RemoveItem(const User& customer, DishId dish_id,
                             const std::string& spec) {
  Check(customer.role == Role::kCustomer, ErrorCode::kPermissionDenied,
        "只有顾客可以使用购物车");
  repos_.carts.Remove(customer.id, dish_id, spec);
}

void CartService::Clear(const User& customer) {
  Check(customer.role == Role::kCustomer, ErrorCode::kPermissionDenied,
        "只有顾客可以使用购物车");
  repos_.carts.Clear(customer.id);
}

std::vector<CartLine> CartService::List(const User& customer) const {
  Check(customer.role == Role::kCustomer, ErrorCode::kPermissionDenied,
        "只有顾客可以使用购物车");
  std::vector<CartLine> lines;
  for (const CartItem& item : repos_.carts.ListByCustomer(customer.id)) {
    const auto dish = repos_.dishes.FindById(item.dish_id);
    if (!dish.has_value()) {
      continue;
    }
    CartLine line;
    line.dish = dish.value();
    line.quantity = item.quantity;
    line.current_price = dish->price;
    line.snapshot_price = item.price_when_added;
    line.price_changed = item.price_when_added != dish->price;
    const auto stock = repos_.stocks.Find(item.dish_id,
                                          ToDateKey(clock_->Now()));
    const int remaining = stock.has_value() ? stock->Remaining() : 0;
    line.sold_out = dish->status != DishStatus::kOnSale ||
                    remaining < item.quantity;
    lines.push_back(line);
  }
  return lines;
}

bool CartService::HasPriceChange(const User& customer) const {
  for (const CartLine& line : List(customer)) {
    if (line.price_changed) {
      return true;
    }
  }
  return false;
}

}  // namespace delivery
