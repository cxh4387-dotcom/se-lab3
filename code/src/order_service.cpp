// Copyright 2026 251220048 Cao Xuhang. All rights reserved.

#include <algorithm>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "delivery/services.h"

namespace delivery {
namespace {

constexpr int kAcceptTimeoutMinutes = 5;

User SystemActor() {
  User system;
  system.id = 0;
  system.phone = "system";
  system.role = Role::kAdmin;
  system.audit_status = AuditStatus::kApproved;
  return system;
}

}  // namespace

OrderService::OrderService(const Repositories& repos, Clock* clock,
                           IdGenerator* ids, AuditService* audit,
                           RefundService* refunds)
    : repos_(repos), clock_(clock), ids_(ids), audit_(audit) {
  refunds_ = refunds;
}

OrderService::~OrderService() = default;

Order OrderService::Submit(const User& customer,
                           const SubmitOrderRequest& request) {
  Check(customer.role == Role::kCustomer, ErrorCode::kPermissionDenied,
        "只有顾客可以下单");
  const std::string address = NormalizeText(request.address);
  Check(!address.empty(), ErrorCode::kInvalidArgument, "收货地址不能为空");
  Check(domain::IsValidPhone(request.phone), ErrorCode::kInvalidArgument,
        "联系电话格式不正确");
  Check(!request.idempotency_key.empty(), ErrorCode::kInvalidArgument,
        "缺少幂等键");

  // One mutex per service keeps the check-then-create section atomic. The
  // stock deduction itself is atomic inside the repository, which is the
  // part that also protects against overselling in the database design.
  std::lock_guard<std::mutex> lock(submit_mutex_);

  const auto existed = repos_.idempotency.Find(request.idempotency_key);
  if (existed.has_value()) {
    const auto order = repos_.orders.FindById(existed->order_id);
    Check(order.has_value(), ErrorCode::kNotFound, "幂等记录指向的订单不存在");
    return order.value();
  }

  const std::vector<CartItem> cart = repos_.carts.ListByCustomer(customer.id);
  Check(!cart.empty(), ErrorCode::kInvalidArgument, "购物车为空");

  ShopId shop_id = 0;
  Money total = 0;
  std::vector<OrderItem> items;
  for (const CartItem& line : cart) {
    const auto dish = repos_.dishes.FindById(line.dish_id);
    Check(dish.has_value(), ErrorCode::kNotFound, "购物车中有菜品已下架");
    if (shop_id == 0) {
      shop_id = dish->shop_id;
    }
    Check(dish->shop_id == shop_id, ErrorCode::kInvalidArgument,
          "一次只能提交同一店铺的订单");
    Check(dish->status == DishStatus::kOnSale, ErrorCode::kOutOfStock,
          "菜品 " + dish->name + " 已售罄");
    OrderItem item;
    item.dish_id = dish->id;
    item.dish_name = dish->name;
    item.unit_price = dish->price;
    item.spec = dish->spec;
    item.quantity = line.quantity;
    total += item.Subtotal();
    items.push_back(item);
  }

  const auto shop = repos_.shops.FindById(shop_id);
  Check(shop.has_value(), ErrorCode::kNotFound, "店铺不存在");
  Check(shop->AcceptsNewOrder(clock_->Now()), ErrorCode::kConflict,
        "店铺当前不接单");

  for (const Order& existing : repos_.orders.ListByCustomer(customer.id)) {
    Check(!(existing.shop_id == shop_id && existing.IsUnfinished()),
          ErrorCode::kConflict, "该店铺已有进行中的订单，请先处理");
  }

  const int biz_date = ToDateKey(clock_->Now());
  std::vector<OrderItem> deducted;
  for (const OrderItem& item : items) {
    if (!repos_.stocks.TryDeduct(item.dish_id, biz_date, item.quantity)) {
      for (const OrderItem& undo : deducted) {
        repos_.stocks.Release(undo.dish_id, biz_date, undo.quantity);
      }
      throw DomainError(ErrorCode::kOutOfStock,
                        "菜品 " + item.dish_name + " 库存不足");
    }
    deducted.push_back(item);
  }

  Order order;
  order.id = ids_->Next();
  order.order_no = MakeOrderNo(clock_->Now(), order.id);
  order.customer_id = customer.id;
  order.shop_id = shop_id;
  order.address_snapshot = address;
  order.phone_snapshot = request.phone;
  order.items = items;
  order.total = total;
  order.status = OrderStatus::kPending;
  order.version = 1;
  order.idempotency_key = request.idempotency_key;
  order.created_at = clock_->Now();
  repos_.orders.Save(order);

  IdempotencyRecord record;
  record.key = request.idempotency_key;
  record.customer_id = customer.id;
  record.order_id = order.id;
  record.created_at = clock_->Now();
  repos_.idempotency.Save(record);

  for (const CartItem& line : cart) {
    repos_.carts.Remove(customer.id, line.dish_id, line.spec);
  }

  audit_->Record(customer, "提交订单", "order", order.order_no, "",
                 ToString(order.status), "成功");
  return order;
}

void OrderService::Accept(const User& merchant, const std::string& order_no) {
  Check(merchant.role == Role::kMerchant, ErrorCode::kPermissionDenied,
        "只有商家可以接单");
  Order order = LoadForUpdate(order_no);
  const auto shop = repos_.shops.FindById(order.shop_id);
  Check(shop.has_value() && shop->owner_id == merchant.id,
        ErrorCode::kPermissionDenied, "只能处理本店订单");
  PersistTransition(&order, OrderStatus::kAccepted, "", merchant);
  order.accepted_at = clock_->Now();
  repos_.orders.Save(order);
}

void OrderService::Reject(const User& merchant, const std::string& order_no,
                          const std::string& reason) {
  Check(merchant.role == Role::kMerchant, ErrorCode::kPermissionDenied,
        "只有商家可以拒单");
  Check(!NormalizeText(reason).empty(), ErrorCode::kInvalidArgument,
        "拒单必须填写原因");
  Order order = LoadForUpdate(order_no);
  const auto shop = repos_.shops.FindById(order.shop_id);
  Check(shop.has_value() && shop->owner_id == merchant.id,
        ErrorCode::kPermissionDenied, "只能处理本店订单");
  PersistTransition(&order, OrderStatus::kRejected, reason, merchant);
  ReleaseStock(order);
  if (refunds_ != nullptr) {
    refunds_->Create(merchant, order.id, order.total, "商家拒单");
  }
}

void OrderService::MarkReady(const User& merchant,
                             const std::string& order_no) {
  Check(merchant.role == Role::kMerchant, ErrorCode::kPermissionDenied,
        "只有商家可以操作制作完成");
  Order order = LoadForUpdate(order_no);
  const auto shop = repos_.shops.FindById(order.shop_id);
  Check(shop.has_value() && shop->owner_id == merchant.id,
        ErrorCode::kPermissionDenied, "只能处理本店订单");
  PersistTransition(&order, OrderStatus::kReady, "", merchant);
  order.ready_at = clock_->Now();
  repos_.orders.Save(order);
}

void OrderService::CancelByCustomer(const User& customer,
                                    const std::string& order_no) {
  Check(customer.role == Role::kCustomer, ErrorCode::kPermissionDenied,
        "只有顾客可以取消订单");
  Order order = LoadForUpdate(order_no);
  Check(order.customer_id == customer.id, ErrorCode::kPermissionDenied,
        "只能取消自己的订单");
  Check(order.status == OrderStatus::kPending, ErrorCode::kStateChanged,
        "商家已接单，请联系商家协商取消");
  PersistTransition(&order, OrderStatus::kCancelled, "顾客主动取消", customer);
  ReleaseStock(order);
  if (refunds_ != nullptr) {
    refunds_->Create(customer, order.id, order.total, "顾客取消");
  }
}

void OrderService::CancelByMerchant(const User& merchant,
                                    const std::string& order_no) {
  Check(merchant.role == Role::kMerchant, ErrorCode::kPermissionDenied,
        "只有商家可以同意取消");
  Order order = LoadForUpdate(order_no);
  const auto shop = repos_.shops.FindById(order.shop_id);
  Check(shop.has_value() && shop->owner_id == merchant.id,
        ErrorCode::kPermissionDenied, "只能处理本店订单");
  PersistTransition(&order, OrderStatus::kCancelled, "商家同意取消", merchant);
  ReleaseStock(order);
  if (refunds_ != nullptr) {
    refunds_->Create(merchant, order.id, order.total, "商家同意取消");
  }
}

void OrderService::ConfirmReceipt(const User& customer,
                                  const std::string& order_no) {
  Check(customer.role == Role::kCustomer, ErrorCode::kPermissionDenied,
        "只有顾客可以确认收货");
  Order order = LoadForUpdate(order_no);
  Check(order.customer_id == customer.id, ErrorCode::kPermissionDenied,
        "只能确认自己的订单");
  PersistTransition(&order, OrderStatus::kCompleted, "", customer);
  order.completed_at = clock_->Now();
  repos_.orders.Save(order);
}

std::vector<std::string> OrderService::CancelTimeoutOrders() {
  std::vector<std::string> cancelled;
  const User system = SystemActor();
  for (const Order& order : repos_.orders.ListAll()) {
    if (order.status != OrderStatus::kPending) {
      continue;
    }
    const auto waited = clock_->Now() - order.created_at;
    if (waited < Minutes(kAcceptTimeoutMinutes)) {
      continue;
    }
    Order target = order;
    PersistTransition(&target, OrderStatus::kCancelled,
                      "超时未接单自动取消", system);
    target.cancel_reason = "超时未接单";
    ReleaseStock(target);
    if (refunds_ != nullptr) {
      refunds_->Create(system, target.id, target.total, "超时未接单");
    }
    cancelled.push_back(target.order_no);
  }
  return cancelled;
}

OrderView OrderService::Get(const User& actor,
                            const std::string& order_no) const {
  const auto found = repos_.orders.FindByNo(order_no);
  Check(found.has_value(), ErrorCode::kNotFound, "订单不存在");
  const Order& order = found.value();
  if (actor.role == Role::kCustomer) {
    Check(order.customer_id == actor.id, ErrorCode::kPermissionDenied,
          "无权查看该订单");
  } else if (actor.role == Role::kMerchant) {
    const auto shop = repos_.shops.FindById(order.shop_id);
    Check(shop.has_value() && shop->owner_id == actor.id,
          ErrorCode::kPermissionDenied, "无权查看该订单");
  } else if (actor.role == Role::kRider) {
    const auto task = repos_.deliveries.FindByOrderId(order.id);
    Check(task.has_value() && task->rider_id == actor.id,
          ErrorCode::kPermissionDenied, "无权查看该订单");
  }

  OrderView view;
  view.order = order;
  const auto shop = repos_.shops.FindById(order.shop_id);
  view.shop_name = shop.has_value() ? shop->name : "";
  view.delivery = repos_.deliveries.FindByOrderId(order.id);
  view.refund = repos_.refunds.FindByOrderId(order.id);
  return view;
}

std::vector<OrderView> OrderService::ListByCustomer(
    const User& customer) const {
  std::vector<OrderView> result;
  for (const Order& order : repos_.orders.ListByCustomer(customer.id)) {
    OrderView view;
    view.order = order;
    const auto shop = repos_.shops.FindById(order.shop_id);
    view.shop_name = shop.has_value() ? shop->name : "";
    view.delivery = repos_.deliveries.FindByOrderId(order.id);
    view.refund = repos_.refunds.FindByOrderId(order.id);
    result.push_back(view);
  }
  std::sort(result.begin(), result.end(),
            [](const OrderView& a, const OrderView& b) {
              return a.order.created_at > b.order.created_at;
            });
  return result;
}

std::vector<OrderView> OrderService::ListByShop(const User& merchant,
                                                ShopId shop_id) const {
  const auto shop = repos_.shops.FindById(shop_id);
  Check(shop.has_value(), ErrorCode::kNotFound, "店铺不存在");
  if (merchant.role != Role::kAdmin) {
    Check(shop->owner_id == merchant.id, ErrorCode::kPermissionDenied,
          "只能查看本店订单");
  }
  std::vector<OrderView> result;
  for (const Order& order : repos_.orders.ListByShop(shop_id)) {
    OrderView view;
    view.order = order;
    view.shop_name = shop->name;
    view.delivery = repos_.deliveries.FindByOrderId(order.id);
    view.refund = repos_.refunds.FindByOrderId(order.id);
    result.push_back(view);
  }
  std::sort(result.begin(), result.end(),
            [](const OrderView& a, const OrderView& b) {
              return a.order.created_at > b.order.created_at;
            });
  return result;
}

void OrderService::MarkDeliveryFailed(OrderId order_id,
                                      const std::string& reason,
                                      const User& actor) {
  const auto found = repos_.orders.FindById(order_id);
  Check(found.has_value(), ErrorCode::kNotFound, "订单不存在");
  Order order = found.value();
  PersistTransition(&order, OrderStatus::kDeliveryFailed, reason, actor);
  order.cancel_reason = reason;
  repos_.orders.Save(order);
}

void OrderService::MarkRefunded(OrderId order_id, const User& actor) {
  const auto found = repos_.orders.FindById(order_id);
  Check(found.has_value(), ErrorCode::kNotFound, "订单不存在");
  Order order = found.value();
  PersistTransition(&order, OrderStatus::kRefunded, "", actor);
}

Order OrderService::LoadForUpdate(const std::string& order_no) {
  const auto found = repos_.orders.FindByNo(order_no);
  Check(found.has_value(), ErrorCode::kNotFound, "订单不存在");
  return found.value();
}

void OrderService::PersistTransition(Order* order, OrderStatus target,
                                     const std::string& reason,
                                     const User& actor) {
  const std::string before = ToString(order->status);
  domain::OrderStateMachine::EnsureTransition(order->status, target);
  order->status = target;
  order->version += 1;
  if (!reason.empty()) {
    order->cancel_reason = reason;
  }
  repos_.orders.Save(*order);
  audit_->Record(actor, "订单状态变更", "order", order->order_no, before,
                 ToString(target), "成功");
}

void OrderService::ReleaseStock(const Order& order) {
  std::lock_guard<std::mutex> lock(submit_mutex_);
  if (!released_orders_.insert(order.id).second) {
    return;  // Already released, keep the operation idempotent.
  }
  const int biz_date = ToDateKey(order.created_at);
  for (const OrderItem& item : order.items) {
    repos_.stocks.Release(item.dish_id, biz_date, item.quantity);
  }
}

}  // namespace delivery
