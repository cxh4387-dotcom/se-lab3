// Copyright 2026 251220048 Cao Xuhang. All rights reserved.
// Repository interfaces. The domain layer depends on these abstractions only,
// the memory implementation lives in infrastructure, which is the dependency
// inversion described in the package diagram of the design report.

#ifndef DELIVERY_REPOSITORIES_H_
#define DELIVERY_REPOSITORIES_H_

#include <optional>
#include <string>
#include <vector>

#include "delivery/common.h"
#include "delivery/domain_types.h"

namespace delivery {

class UserRepository {
 public:
  virtual ~UserRepository() = default;
  virtual void Save(const User& user) = 0;
  virtual std::optional<User> FindById(UserId id) const = 0;
  virtual std::optional<User> FindByPhone(const std::string& phone) const = 0;
  virtual std::optional<User> FindByLicenseNo(
      const std::string& license_no) const = 0;
  virtual std::optional<User> FindByIdCardNo(
      const std::string& id_card_no) const = 0;
  virtual std::vector<User> ListAll() const = 0;
};

class ShopRepository {
 public:
  virtual ~ShopRepository() = default;
  virtual void Save(const Shop& shop) = 0;
  virtual std::optional<Shop> FindById(ShopId id) const = 0;
  virtual std::optional<Shop> FindByLicenseNo(
      const std::string& license_no) const = 0;
  virtual std::vector<Shop> ListByOwner(UserId owner_id) const = 0;
  virtual std::vector<Shop> ListAll() const = 0;
};

class DishRepository {
 public:
  virtual ~DishRepository() = default;
  virtual void Save(const Dish& dish) = 0;
  virtual std::optional<Dish> FindById(DishId id) const = 0;
  virtual std::vector<Dish> ListByShop(ShopId shop_id) const = 0;
  virtual std::vector<Dish> ListAll() const = 0;
  virtual void Remove(DishId id) = 0;
};

class StockRepository {
 public:
  virtual ~StockRepository() = default;
  virtual void Save(const DailyStock& stock) = 0;
  virtual std::optional<DailyStock> Find(DishId dish_id,
                                         int biz_date) const = 0;
  virtual std::vector<DailyStock> ListByDate(int biz_date) const = 0;

  // Atomic check-and-deduct. Returns false when the remaining quantity is
  // smaller than the requested one, which is how the prototype avoids
  // overselling under concurrent submissions.
  virtual bool TryDeduct(DishId dish_id, int biz_date, int quantity) = 0;
  // Adds the quantity back. Used when an order is cancelled or rejected.
  virtual void Release(DishId dish_id, int biz_date, int quantity) = 0;
};

class CartRepository {
 public:
  virtual ~CartRepository() = default;
  virtual void Upsert(const CartItem& item) = 0;
  virtual std::vector<CartItem> ListByCustomer(UserId customer_id) const = 0;
  virtual void Remove(UserId customer_id, DishId dish_id,
                      const std::string& spec) = 0;
  virtual void Clear(UserId customer_id) = 0;
};

class OrderRepository {
 public:
  virtual ~OrderRepository() = default;
  virtual void Save(const Order& order) = 0;
  virtual std::optional<Order> FindById(OrderId id) const = 0;
  virtual std::optional<Order> FindByNo(const std::string& order_no) const = 0;
  virtual std::vector<Order> ListByCustomer(UserId customer_id) const = 0;
  virtual std::vector<Order> ListByShop(ShopId shop_id) const = 0;
  virtual std::vector<Order> ListAll() const = 0;
};

class DeliveryRepository {
 public:
  virtual ~DeliveryRepository() = default;
  virtual void Save(const DeliveryTask& task) = 0;
  virtual std::optional<DeliveryTask> FindById(TaskId id) const = 0;
  virtual std::optional<DeliveryTask> FindByOrderId(OrderId order_id) const = 0;
  virtual std::vector<DeliveryTask> ListByRider(UserId rider_id) const = 0;
  virtual std::vector<DeliveryTask> ListAll() const = 0;
};

class RefundRepository {
 public:
  virtual ~RefundRepository() = default;
  virtual void Save(const Refund& refund) = 0;
  virtual std::optional<Refund> FindById(RefundId id) const = 0;
  virtual std::optional<Refund> FindByOrderId(OrderId order_id) const = 0;
  virtual std::vector<Refund> ListAll() const = 0;
};

class LogRepository {
 public:
  virtual ~LogRepository() = default;
  virtual void Append(const LogEntry& entry) = 0;
  virtual std::vector<LogEntry> ListAll() const = 0;
};

class IdempotencyRepository {
 public:
  virtual ~IdempotencyRepository() = default;
  virtual void Save(const IdempotencyRecord& record) = 0;
  virtual std::optional<IdempotencyRecord> Find(
      const std::string& key) const = 0;
};

}  // namespace delivery

#endif  // DELIVERY_REPOSITORIES_H_
