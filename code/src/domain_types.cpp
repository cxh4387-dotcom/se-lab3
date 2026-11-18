// Copyright 2026 251220048 Cao Xuhang. All rights reserved.

#include "delivery/domain_types.h"

#include <cstdio>
#include <ctime>
#include <string>

namespace delivery {
namespace {

std::tm LocalTm(const TimePoint& point) {
  std::time_t raw = std::chrono::system_clock::to_time_t(point);
  std::tm tm_value{};
#ifdef _WIN32
  localtime_s(&tm_value, &raw);
#else
  localtime_r(&raw, &tm_value);
#endif
  return tm_value;
}

}  // namespace

std::string ToString(Role value) {
  switch (value) {
    case Role::kCustomer:
      return "顾客";
    case Role::kMerchant:
      return "商家";
    case Role::kRider:
      return "配送人员";
    case Role::kAdmin:
      return "平台管理员";
  }
  return "未知角色";
}

std::string ToString(AuditStatus value) {
  switch (value) {
    case AuditStatus::kPending:
      return "待审核";
    case AuditStatus::kApproved:
      return "已通过";
    case AuditStatus::kRejected:
      return "已驳回";
  }
  return "未知状态";
}

std::string ToString(ShopBizStatus value) {
  switch (value) {
    case ShopBizStatus::kOpen:
      return "营业中";
    case ShopBizStatus::kClosed:
      return "已打烊";
    case ShopBizStatus::kTempClosed:
      return "临时停业";
    case ShopBizStatus::kForceClosed:
      return "强制停业";
  }
  return "未知状态";
}

std::string ToString(DishStatus value) {
  switch (value) {
    case DishStatus::kOnSale:
      return "在售";
    case DishStatus::kOffShelf:
      return "已下架";
    case DishStatus::kSoldOut:
      return "已售罄";
  }
  return "未知状态";
}

std::string ToString(OrderStatus value) {
  switch (value) {
    case OrderStatus::kPending:
      return "待接单";
    case OrderStatus::kAccepted:
      return "已接单";
    case OrderStatus::kReady:
      return "待取餐";
    case OrderStatus::kDelivering:
      return "配送中";
    case OrderStatus::kCompleted:
      return "已完成";
    case OrderStatus::kCancelled:
      return "已取消";
    case OrderStatus::kRejected:
      return "已拒单";
    case OrderStatus::kDeliveryFailed:
      return "配送失败";
    case OrderStatus::kRefunded:
      return "已退款";
  }
  return "未知状态";
}

std::string ToString(DeliveryStatus value) {
  switch (value) {
    case DeliveryStatus::kWaiting:
      return "待接单";
    case DeliveryStatus::kAccepted:
      return "已接单";
    case DeliveryStatus::kPicked:
      return "已取餐";
    case DeliveryStatus::kDelivered:
      return "已送达";
    case DeliveryStatus::kFailed:
      return "配送失败";
  }
  return "未知状态";
}

std::string ToString(RefundStatus value) {
  switch (value) {
    case RefundStatus::kRefunding:
      return "退款中";
    case RefundStatus::kRefunded:
      return "已退款";
    case RefundStatus::kFailed:
      return "退款异常";
  }
  return "未知状态";
}

int ToDateKey(const TimePoint& point) {
  const std::tm tm_value = LocalTm(point);
  return (tm_value.tm_year + 1900) * 10000 + (tm_value.tm_mon + 1) * 100 +
         tm_value.tm_mday;
}

int ToMinuteOfDay(const TimePoint& point) {
  const std::tm tm_value = LocalTm(point);
  return tm_value.tm_hour * 60 + tm_value.tm_min;
}

std::string ToDateText(const TimePoint& point) {
  const std::tm tm_value = LocalTm(point);
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d",
                tm_value.tm_year + 1900, tm_value.tm_mon + 1,
                tm_value.tm_mday);
  return std::string(buffer);
}

bool Shop::WithinOpeningHours(int minute_of_day) const {
  if (open_minute == close_minute) {
    return true;  // Open around the clock.
  }
  if (!close_next_day && close_minute > open_minute) {
    return minute_of_day >= open_minute && minute_of_day < close_minute;
  }
  return minute_of_day >= open_minute || minute_of_day < close_minute;
}

bool Shop::AcceptsNewOrder(const TimePoint& now) const {
  if (biz_status != ShopBizStatus::kOpen) {
    return false;
  }
  return WithinOpeningHours(ToMinuteOfDay(now));
}

Money Order::RecalculateTotal() const {
  Money sum = 0;
  for (const OrderItem& item : items) {
    sum += item.Subtotal();
  }
  return sum;
}

bool Order::IsUnfinished() const {
  return status == OrderStatus::kPending || status == OrderStatus::kAccepted ||
         status == OrderStatus::kReady ||
         status == OrderStatus::kDelivering;
}

bool Order::IsTerminal() const {
  return status == OrderStatus::kCompleted ||
         status == OrderStatus::kRefunded ||
         status == OrderStatus::kCancelled;
}

}  // namespace delivery
