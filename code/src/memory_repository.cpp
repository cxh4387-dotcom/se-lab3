// Copyright 2026 251220048 Cao Xuhang. All rights reserved.

#include "delivery/memory_repository.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace delivery {

void MemoryDatabase::Reset() {
  std::lock_guard<std::mutex> lock(mutex);
  users.clear();
  shops.clear();
  dishes.clear();
  stocks.clear();
  carts.clear();
  orders.clear();
  tasks.clear();
  refunds.clear();
  logs.clear();
  idempotency.clear();
}

Repositories MemoryRepositories::Bundle() {
  return Repositories{users_, shops_, dishes_, stocks_, carts_, orders_,
                      deliveries_, refunds_, logs_, idempotency_};
}

// ---------------------------------------------------------------- users

void MemoryUserRepository::Save(const User& user) {
  std::lock_guard<std::mutex> lock(db_->mutex);
  db_->users[user.id] = user;
}

std::optional<User> MemoryUserRepository::FindById(UserId id) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  const auto found = db_->users.find(id);
  if (found == db_->users.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::optional<User> MemoryUserRepository::FindByPhone(
    const std::string& phone) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  for (const auto& entry : db_->users) {
    if (entry.second.phone == phone) {
      return entry.second;
    }
  }
  return std::nullopt;
}

std::optional<User> MemoryUserRepository::FindByLicenseNo(
    const std::string& license_no) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  for (const auto& entry : db_->users) {
    if (!license_no.empty() && entry.second.license_no == license_no) {
      return entry.second;
    }
  }
  return std::nullopt;
}

std::optional<User> MemoryUserRepository::FindByIdCardNo(
    const std::string& id_card_no) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  for (const auto& entry : db_->users) {
    if (!id_card_no.empty() && entry.second.id_card_no == id_card_no) {
      return entry.second;
    }
  }
  return std::nullopt;
}

std::vector<User> MemoryUserRepository::ListAll() const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  std::vector<User> result;
  result.reserve(db_->users.size());
  for (const auto& entry : db_->users) {
    result.push_back(entry.second);
  }
  return result;
}

// ---------------------------------------------------------------- shops

void MemoryShopRepository::Save(const Shop& shop) {
  std::lock_guard<std::mutex> lock(db_->mutex);
  db_->shops[shop.id] = shop;
}

std::optional<Shop> MemoryShopRepository::FindById(ShopId id) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  const auto found = db_->shops.find(id);
  if (found == db_->shops.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::optional<Shop> MemoryShopRepository::FindByLicenseNo(
    const std::string& license_no) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  for (const auto& entry : db_->shops) {
    if (!license_no.empty() && entry.second.license_no == license_no) {
      return entry.second;
    }
  }
  return std::nullopt;
}

std::vector<Shop> MemoryShopRepository::ListByOwner(UserId owner_id) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  std::vector<Shop> result;
  for (const auto& entry : db_->shops) {
    if (entry.second.owner_id == owner_id) {
      result.push_back(entry.second);
    }
  }
  return result;
}

std::vector<Shop> MemoryShopRepository::ListAll() const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  std::vector<Shop> result;
  result.reserve(db_->shops.size());
  for (const auto& entry : db_->shops) {
    result.push_back(entry.second);
  }
  return result;
}

// ---------------------------------------------------------------- dishes

void MemoryDishRepository::Save(const Dish& dish) {
  std::lock_guard<std::mutex> lock(db_->mutex);
  db_->dishes[dish.id] = dish;
}

std::optional<Dish> MemoryDishRepository::FindById(DishId id) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  const auto found = db_->dishes.find(id);
  if (found == db_->dishes.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::vector<Dish> MemoryDishRepository::ListByShop(ShopId shop_id) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  std::vector<Dish> result;
  for (const auto& entry : db_->dishes) {
    if (entry.second.shop_id == shop_id) {
      result.push_back(entry.second);
    }
  }
  return result;
}

std::vector<Dish> MemoryDishRepository::ListAll() const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  std::vector<Dish> result;
  result.reserve(db_->dishes.size());
  for (const auto& entry : db_->dishes) {
    result.push_back(entry.second);
  }
  return result;
}

void MemoryDishRepository::Remove(DishId id) {
  std::lock_guard<std::mutex> lock(db_->mutex);
  db_->dishes.erase(id);
}

// ---------------------------------------------------------------- stocks

void MemoryStockRepository::Save(const DailyStock& stock) {
  std::lock_guard<std::mutex> lock(db_->mutex);
  db_->stocks[std::make_pair(stock.dish_id, stock.biz_date)] = stock;
}

std::optional<DailyStock> MemoryStockRepository::Find(DishId dish_id,
                                                      int biz_date) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  const auto found =
      db_->stocks.find(std::make_pair(dish_id, biz_date));
  if (found == db_->stocks.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::vector<DailyStock> MemoryStockRepository::ListByDate(int biz_date) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  std::vector<DailyStock> result;
  for (const auto& entry : db_->stocks) {
    if (entry.second.biz_date == biz_date) {
      result.push_back(entry.second);
    }
  }
  return result;
}

// The lock is what makes check-and-deduct atomic: two threads cannot both
// observe the same remaining quantity and both succeed.
bool MemoryStockRepository::TryDeduct(DishId dish_id, int biz_date,
                                      int quantity) {
  std::lock_guard<std::mutex> lock(db_->mutex);
  const auto found = db_->stocks.find(std::make_pair(dish_id, biz_date));
  if (found == db_->stocks.end()) {
    return false;
  }
  if (found->second.Remaining() < quantity) {
    return false;
  }
  found->second.sold_qty += quantity;
  return true;
}

void MemoryStockRepository::Release(DishId dish_id, int biz_date,
                                    int quantity) {
  std::lock_guard<std::mutex> lock(db_->mutex);
  const auto found = db_->stocks.find(std::make_pair(dish_id, biz_date));
  if (found == db_->stocks.end()) {
    return;
  }
  found->second.sold_qty -= quantity;
  if (found->second.sold_qty < 0) {
    found->second.sold_qty = 0;
  }
  found->second.released_qty += quantity;
}

// ---------------------------------------------------------------- carts

void MemoryCartRepository::Upsert(const CartItem& item) {
  std::lock_guard<std::mutex> lock(db_->mutex);
  for (CartItem& existing : db_->carts) {
    if (existing.customer_id == item.customer_id &&
        existing.dish_id == item.dish_id && existing.spec == item.spec) {
      existing.quantity = item.quantity;
      existing.price_when_added = item.price_when_added;
      return;
    }
  }
  db_->carts.push_back(item);
}

std::vector<CartItem> MemoryCartRepository::ListByCustomer(
    UserId customer_id) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  std::vector<CartItem> result;
  for (const CartItem& item : db_->carts) {
    if (item.customer_id == customer_id) {
      result.push_back(item);
    }
  }
  return result;
}

void MemoryCartRepository::Remove(UserId customer_id, DishId dish_id,
                                  const std::string& spec) {
  std::lock_guard<std::mutex> lock(db_->mutex);
  db_->carts.erase(
      std::remove_if(db_->carts.begin(), db_->carts.end(),
                     [&](const CartItem& item) {
                       return item.customer_id == customer_id &&
                              item.dish_id == dish_id && item.spec == spec;
                     }),
      db_->carts.end());
}

void MemoryCartRepository::Clear(UserId customer_id) {
  std::lock_guard<std::mutex> lock(db_->mutex);
  db_->carts.erase(std::remove_if(db_->carts.begin(), db_->carts.end(),
                                  [&](const CartItem& item) {
                                    return item.customer_id == customer_id;
                                  }),
                   db_->carts.end());
}

// ---------------------------------------------------------------- orders

void MemoryOrderRepository::Save(const Order& order) {
  std::lock_guard<std::mutex> lock(db_->mutex);
  db_->orders[order.id] = order;
}

std::optional<Order> MemoryOrderRepository::FindById(OrderId id) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  const auto found = db_->orders.find(id);
  if (found == db_->orders.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::optional<Order> MemoryOrderRepository::FindByNo(
    const std::string& order_no) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  for (const auto& entry : db_->orders) {
    if (entry.second.order_no == order_no) {
      return entry.second;
    }
  }
  return std::nullopt;
}

std::vector<Order> MemoryOrderRepository::ListByCustomer(
    UserId customer_id) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  std::vector<Order> result;
  for (const auto& entry : db_->orders) {
    if (entry.second.customer_id == customer_id) {
      result.push_back(entry.second);
    }
  }
  return result;
}

std::vector<Order> MemoryOrderRepository::ListByShop(ShopId shop_id) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  std::vector<Order> result;
  for (const auto& entry : db_->orders) {
    if (entry.second.shop_id == shop_id) {
      result.push_back(entry.second);
    }
  }
  return result;
}

std::vector<Order> MemoryOrderRepository::ListAll() const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  std::vector<Order> result;
  result.reserve(db_->orders.size());
  for (const auto& entry : db_->orders) {
    result.push_back(entry.second);
  }
  return result;
}

// ---------------------------------------------------------------- delivery

void MemoryDeliveryRepository::Save(const DeliveryTask& task) {
  std::lock_guard<std::mutex> lock(db_->mutex);
  db_->tasks[task.id] = task;
}

std::optional<DeliveryTask> MemoryDeliveryRepository::FindById(
    TaskId id) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  const auto found = db_->tasks.find(id);
  if (found == db_->tasks.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::optional<DeliveryTask> MemoryDeliveryRepository::FindByOrderId(
    OrderId order_id) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  for (const auto& entry : db_->tasks) {
    if (entry.second.order_id == order_id) {
      return entry.second;
    }
  }
  return std::nullopt;
}

std::vector<DeliveryTask> MemoryDeliveryRepository::ListByRider(
    UserId rider_id) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  std::vector<DeliveryTask> result;
  for (const auto& entry : db_->tasks) {
    if (entry.second.rider_id == rider_id) {
      result.push_back(entry.second);
    }
  }
  return result;
}

std::vector<DeliveryTask> MemoryDeliveryRepository::ListAll() const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  std::vector<DeliveryTask> result;
  result.reserve(db_->tasks.size());
  for (const auto& entry : db_->tasks) {
    result.push_back(entry.second);
  }
  return result;
}

// ---------------------------------------------------------------- refunds

void MemoryRefundRepository::Save(const Refund& refund) {
  std::lock_guard<std::mutex> lock(db_->mutex);
  db_->refunds[refund.id] = refund;
}

std::optional<Refund> MemoryRefundRepository::FindById(RefundId id) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  const auto found = db_->refunds.find(id);
  if (found == db_->refunds.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::optional<Refund> MemoryRefundRepository::FindByOrderId(
    OrderId order_id) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  for (const auto& entry : db_->refunds) {
    if (entry.second.order_id == order_id) {
      return entry.second;
    }
  }
  return std::nullopt;
}

std::vector<Refund> MemoryRefundRepository::ListAll() const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  std::vector<Refund> result;
  result.reserve(db_->refunds.size());
  for (const auto& entry : db_->refunds) {
    result.push_back(entry.second);
  }
  return result;
}

// ---------------------------------------------------------------- logs

void MemoryLogRepository::Append(const LogEntry& entry) {
  std::lock_guard<std::mutex> lock(db_->mutex);
  db_->logs.push_back(entry);
}

std::vector<LogEntry> MemoryLogRepository::ListAll() const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  return db_->logs;
}

// ------------------------------------------------------------ idempotency

void MemoryIdempotencyRepository::Save(const IdempotencyRecord& record) {
  std::lock_guard<std::mutex> lock(db_->mutex);
  db_->idempotency[record.key] = record;
}

std::optional<IdempotencyRecord> MemoryIdempotencyRepository::Find(
    const std::string& key) const {
  std::lock_guard<std::mutex> lock(db_->mutex);
  const auto found = db_->idempotency.find(key);
  if (found == db_->idempotency.end()) {
    return std::nullopt;
  }
  return found->second;
}

}  // namespace delivery
