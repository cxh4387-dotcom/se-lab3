// Copyright 2026 251220048 Cao Xuhang. All rights reserved.

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "delivery/services.h"

namespace delivery {

StatsService::StatsService(const Repositories& repos, Clock* clock)
    : repos_(repos), clock_(clock) {}

ShopDailyStats StatsService::DailyOfShop(const User& merchant, ShopId shop_id,
                                         int biz_date) const {
  const auto shop = repos_.shops.FindById(shop_id);
  Check(shop.has_value(), ErrorCode::kNotFound, "店铺不存在");
  if (merchant.role != Role::kAdmin) {
    Check(shop->owner_id == merchant.id, ErrorCode::kPermissionDenied,
          "只能查看本店统计");
  }

  ShopDailyStats stats;
  stats.shop_id = shop_id;
  stats.biz_date = biz_date;
  for (const Order& order : repos_.orders.ListByShop(shop_id)) {
    if (ToDateKey(order.created_at) != biz_date) {
      continue;
    }
    stats.order_count += 1;
    // Timeout is counted by the recorded reason, because a timed out order
    // may already have moved on to "refunded" by the time statistics run.
    if (order.cancel_reason.find("超时") != std::string::npos) {
      stats.timeout_rate += 1.0;
    }
    switch (order.status) {
      case OrderStatus::kCompleted:
        stats.completed += 1;
        stats.revenue += order.total;
        break;
      case OrderStatus::kCancelled:
        stats.cancelled += 1;
        break;
      case OrderStatus::kRejected:
        stats.rejected += 1;
        break;
      case OrderStatus::kDeliveryFailed:
        stats.delivery_failed += 1;
        break;
      default:
        break;
    }
  }
  const double base = stats.order_count == 0 ? 1.0 : stats.order_count;
  stats.timeout_rate = stats.timeout_rate / base;
  stats.failure_rate = stats.delivery_failed / base;
  return stats;
}

std::vector<DishSalesRow> StatsService::DishSales(ShopId shop_id, int from_date,
                                                  int to_date) const {
  std::map<DishId, DishSalesRow> rows;
  for (const Order& order : repos_.orders.ListByShop(shop_id)) {
    if (order.status != OrderStatus::kCompleted) {
      continue;
    }
    const int date = ToDateKey(order.created_at);
    if (date < from_date || date > to_date) {
      continue;
    }
    for (const OrderItem& item : order.items) {
      DishSalesRow& row = rows[item.dish_id];
      row.dish_id = item.dish_id;
      row.dish_name = item.dish_name;
      row.quantity += item.quantity;
      row.revenue += item.Subtotal();
    }
  }

  std::vector<DishSalesRow> result;
  result.reserve(rows.size());
  for (const auto& entry : rows) {
    result.push_back(entry.second);
  }
  std::sort(result.begin(), result.end(),
            [](const DishSalesRow& a, const DishSalesRow& b) {
              return a.quantity > b.quantity;
            });
  return result;
}

std::vector<ShopRankRow> StatsService::PlatformRank(int biz_date) const {
  std::map<ShopId, ShopRankRow> rows;
  for (const Shop& shop : repos_.shops.ListAll()) {
    ShopRankRow& row = rows[shop.id];
    row.shop_id = shop.id;
    row.shop_name = shop.name;
  }
  for (const Order& order : repos_.orders.ListAll()) {
    if (order.status != OrderStatus::kCompleted) {
      continue;
    }
    if (ToDateKey(order.created_at) != biz_date) {
      continue;
    }
    ShopRankRow& row = rows[order.shop_id];
    row.revenue += order.total;
    row.order_count += 1;
  }

  std::vector<ShopRankRow> result;
  result.reserve(rows.size());
  for (const auto& entry : rows) {
    result.push_back(entry.second);
  }
  std::sort(result.begin(), result.end(),
            [](const ShopRankRow& a, const ShopRankRow& b) {
              return a.revenue > b.revenue;
            });
  return result;
}

}  // namespace delivery
