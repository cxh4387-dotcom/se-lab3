// Copyright 2026 251220048 Cao Xuhang. All rights reserved.
// Domain model: entities and value objects derived from the class diagram.

#ifndef DELIVERY_DOMAIN_TYPES_H_
#define DELIVERY_DOMAIN_TYPES_H_

#include <cstdint>
#include <string>
#include <vector>

#include "delivery/common.h"

namespace delivery {

enum class Role { kCustomer, kMerchant, kRider, kAdmin };

enum class AuditStatus { kPending, kApproved, kRejected };

// ShopBizStatus follows REQ-004: open, closed by timetable, temporarily
// closed by the merchant, or force closed by the platform administrator.
enum class ShopBizStatus {
  kOpen,
  kClosed,
  kTempClosed,
  kForceClosed,
};

enum class DishStatus { kOnSale, kOffShelf, kSoldOut };

// Nine order states of REQ-029. The order of the enumerators is also the
// order used when the state machine table is validated by unit tests.
enum class OrderStatus {
  kPending,
  kAccepted,
  kReady,
  kDelivering,
  kCompleted,
  kCancelled,
  kRejected,
  kDeliveryFailed,
  kRefunded,
};

enum class DeliveryStatus { kWaiting, kAccepted, kPicked, kDelivered, kFailed };

enum class RefundStatus { kRefunding, kRefunded, kFailed };

std::string ToString(Role value);
std::string ToString(AuditStatus value);
std::string ToString(ShopBizStatus value);
std::string ToString(DishStatus value);
std::string ToString(OrderStatus value);
std::string ToString(DeliveryStatus value);
std::string ToString(RefundStatus value);

// yyyymmdd, used as the business date of daily stock.
int ToDateKey(const TimePoint& point);
// Minutes since midnight, used by opening hours including cross midnight.
int ToMinuteOfDay(const TimePoint& point);
std::string ToDateText(const TimePoint& point);

struct User {
  UserId id = 0;
  std::string phone;
  std::string password_hash;
  Role role = Role::kCustomer;
  AuditStatus audit_status = AuditStatus::kApproved;
  std::string license_no;   // merchant only, globally unique
  std::string id_card_no;   // rider only, globally unique
  std::string health_no;    // rider only
  int failed_attempts = 0;
  TimePoint locked_until{};
  TimePoint created_at{};

  bool IsLocked(const TimePoint& now) const {
    return locked_until > now;
  }
};

struct Shop {
  ShopId id = 0;
  UserId owner_id = 0;
  std::string name;
  std::string license_no;
  std::string address;
  int open_minute = 9 * 60;
  int close_minute = 22 * 60;
  bool close_next_day = false;
  ShopBizStatus biz_status = ShopBizStatus::kClosed;
  std::string stop_reason;
  TimePoint updated_at{};

  // Returns true when now falls inside the configured opening window.
  bool WithinOpeningHours(int minute_of_day) const;
  bool AcceptsNewOrder(const TimePoint& now) const;
};

struct Dish {
  DishId id = 0;
  ShopId shop_id = 0;
  std::string name;
  std::string category;
  Money price = 0;
  std::string spec;
  DishStatus status = DishStatus::kOnSale;
  std::string image_url;
  TimePoint created_at{};
  TimePoint price_updated_at{};
  Money previous_price = 0;
};

struct DailyStock {
  DishId dish_id = 0;
  int biz_date = 0;
  int total_qty = 0;
  int sold_qty = 0;
  int released_qty = 0;

  int Remaining() const { return total_qty - sold_qty; }
};

struct CartItem {
  UserId customer_id = 0;
  DishId dish_id = 0;
  std::string spec;
  int quantity = 0;
  Money price_when_added = 0;
};

struct OrderItem {
  DishId dish_id = 0;
  std::string dish_name;
  Money unit_price = 0;
  std::string spec;
  int quantity = 0;

  Money Subtotal() const { return unit_price * quantity; }
};

struct Order {
  OrderId id = 0;
  std::string order_no;
  UserId customer_id = 0;
  ShopId shop_id = 0;
  std::string address_snapshot;
  std::string phone_snapshot;
  std::vector<OrderItem> items;
  Money total = 0;
  OrderStatus status = OrderStatus::kPending;
  int version = 0;
  std::string idempotency_key;
  std::string cancel_reason;
  TimePoint created_at{};
  TimePoint accepted_at{};
  TimePoint ready_at{};
  TimePoint picked_at{};
  TimePoint delivered_at{};
  TimePoint completed_at{};

  Money RecalculateTotal() const;
  bool IsUnfinished() const;
  bool IsTerminal() const;
};

struct DeliveryTask {
  TaskId id = 0;
  OrderId order_id = 0;
  UserId rider_id = 0;
  DeliveryStatus status = DeliveryStatus::kWaiting;
  std::string fail_reason;
  int fail_count = 0;
  TimePoint assigned_at{};
  TimePoint accepted_at{};
  TimePoint delivered_at{};
};

struct Refund {
  RefundId id = 0;
  std::string refund_no;
  OrderId order_id = 0;
  Money amount = 0;
  RefundStatus status = RefundStatus::kRefunding;
  std::string fail_reason;
  int retry_count = 0;
  std::string trigger;
  TimePoint created_at{};
  TimePoint updated_at{};
};

struct LogEntry {
  LogId id = 0;
  UserId actor_id = 0;
  Role actor_role = Role::kCustomer;
  std::string action;
  std::string object_type;
  std::string object_id;
  std::string before_state;
  std::string after_state;
  std::string result;
  std::string source_ip;
  TimePoint created_at{};
};

struct IdempotencyRecord {
  std::string key;
  UserId customer_id = 0;
  OrderId order_id = 0;
  TimePoint created_at{};
};

struct StatisticsDaily {
  ShopId shop_id = 0;
  int biz_date = 0;
  Money revenue = 0;
  int order_count = 0;
  int timeout_count = 0;
  int delivery_fail_count = 0;
  int completed_count = 0;
};

}  // namespace delivery

#endif  // DELIVERY_DOMAIN_TYPES_H_
