// Copyright 2026 251220048 Cao Xuhang. All rights reserved.
// In-memory persistence. One mutex guards the whole database so that two
// threads submitting orders for the same dish cannot oversell the stock; the
// production design reaches the same guarantee with a conditional UPDATE.

#ifndef DELIVERY_MEMORY_REPOSITORY_H_
#define DELIVERY_MEMORY_REPOSITORY_H_

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "delivery/common.h"
#include "delivery/domain_types.h"
#include "delivery/repositories.h"

namespace delivery {

// Plain data holder shared by every repository object.
struct MemoryDatabase {
  mutable std::mutex mutex;
  std::map<UserId, User> users;
  std::map<ShopId, Shop> shops;
  std::map<DishId, Dish> dishes;
  std::map<std::pair<DishId, int>, DailyStock> stocks;
  std::vector<CartItem> carts;
  std::map<OrderId, Order> orders;
  std::map<TaskId, DeliveryTask> tasks;
  std::map<RefundId, Refund> refunds;
  std::vector<LogEntry> logs;
  std::map<std::string, IdempotencyRecord> idempotency;

  void Reset();
};

class MemoryUserRepository : public UserRepository {
 public:
  explicit MemoryUserRepository(MemoryDatabase* db) : db_(db) {}
  void Save(const User& user) override;
  std::optional<User> FindById(UserId id) const override;
  std::optional<User> FindByPhone(const std::string& phone) const override;
  std::optional<User> FindByLicenseNo(
      const std::string& license_no) const override;
  std::optional<User> FindByIdCardNo(
      const std::string& id_card_no) const override;
  std::vector<User> ListAll() const override;

 private:
  MemoryDatabase* db_;
};

class MemoryShopRepository : public ShopRepository {
 public:
  explicit MemoryShopRepository(MemoryDatabase* db) : db_(db) {}
  void Save(const Shop& shop) override;
  std::optional<Shop> FindById(ShopId id) const override;
  std::optional<Shop> FindByLicenseNo(
      const std::string& license_no) const override;
  std::vector<Shop> ListByOwner(UserId owner_id) const override;
  std::vector<Shop> ListAll() const override;

 private:
  MemoryDatabase* db_;
};

class MemoryDishRepository : public DishRepository {
 public:
  explicit MemoryDishRepository(MemoryDatabase* db) : db_(db) {}
  void Save(const Dish& dish) override;
  std::optional<Dish> FindById(DishId id) const override;
  std::vector<Dish> ListByShop(ShopId shop_id) const override;
  std::vector<Dish> ListAll() const override;
  void Remove(DishId id) override;

 private:
  MemoryDatabase* db_;
};

class MemoryStockRepository : public StockRepository {
 public:
  explicit MemoryStockRepository(MemoryDatabase* db) : db_(db) {}
  void Save(const DailyStock& stock) override;
  std::optional<DailyStock> Find(DishId dish_id, int biz_date) const override;
  std::vector<DailyStock> ListByDate(int biz_date) const override;
  bool TryDeduct(DishId dish_id, int biz_date, int quantity) override;
  void Release(DishId dish_id, int biz_date, int quantity) override;

 private:
  MemoryDatabase* db_;
};

class MemoryCartRepository : public CartRepository {
 public:
  explicit MemoryCartRepository(MemoryDatabase* db) : db_(db) {}
  void Upsert(const CartItem& item) override;
  std::vector<CartItem> ListByCustomer(UserId customer_id) const override;
  void Remove(UserId customer_id, DishId dish_id,
              const std::string& spec) override;
  void Clear(UserId customer_id) override;

 private:
  MemoryDatabase* db_;
};

class MemoryOrderRepository : public OrderRepository {
 public:
  explicit MemoryOrderRepository(MemoryDatabase* db) : db_(db) {}
  void Save(const Order& order) override;
  std::optional<Order> FindById(OrderId id) const override;
  std::optional<Order> FindByNo(const std::string& order_no) const override;
  std::vector<Order> ListByCustomer(UserId customer_id) const override;
  std::vector<Order> ListByShop(ShopId shop_id) const override;
  std::vector<Order> ListAll() const override;

 private:
  MemoryDatabase* db_;
};

class MemoryDeliveryRepository : public DeliveryRepository {
 public:
  explicit MemoryDeliveryRepository(MemoryDatabase* db) : db_(db) {}
  void Save(const DeliveryTask& task) override;
  std::optional<DeliveryTask> FindById(TaskId id) const override;
  std::optional<DeliveryTask> FindByOrderId(OrderId order_id) const override;
  std::vector<DeliveryTask> ListByRider(UserId rider_id) const override;
  std::vector<DeliveryTask> ListAll() const override;

 private:
  MemoryDatabase* db_;
};

class MemoryRefundRepository : public RefundRepository {
 public:
  explicit MemoryRefundRepository(MemoryDatabase* db) : db_(db) {}
  void Save(const Refund& refund) override;
  std::optional<Refund> FindById(RefundId id) const override;
  std::optional<Refund> FindByOrderId(OrderId order_id) const override;
  std::vector<Refund> ListAll() const override;

 private:
  MemoryDatabase* db_;
};

class MemoryLogRepository : public LogRepository {
 public:
  explicit MemoryLogRepository(MemoryDatabase* db) : db_(db) {}
  void Append(const LogEntry& entry) override;
  std::vector<LogEntry> ListAll() const override;

 private:
  MemoryDatabase* db_;
};

class MemoryIdempotencyRepository : public IdempotencyRepository {
 public:
  explicit MemoryIdempotencyRepository(MemoryDatabase* db) : db_(db) {}
  void Save(const IdempotencyRecord& record) override;
  std::optional<IdempotencyRecord> Find(
      const std::string& key) const override;

 private:
  MemoryDatabase* db_;
};

// Bundle handed to the application services. It holds references, so the
// factory below must outlive every service that uses it.
struct Repositories {
  UserRepository& users;
  ShopRepository& shops;
  DishRepository& dishes;
  StockRepository& stocks;
  CartRepository& carts;
  OrderRepository& orders;
  DeliveryRepository& deliveries;
  RefundRepository& refunds;
  LogRepository& logs;
  IdempotencyRepository& idempotency;
};

// Owns the database and the ten repository objects, in that order.
class MemoryRepositories {
 public:
  MemoryRepositories() : users_(&db_), shops_(&db_), dishes_(&db_),
                         stocks_(&db_), carts_(&db_), orders_(&db_),
                         deliveries_(&db_), refunds_(&db_), logs_(&db_),
                         idempotency_(&db_) {}

  Repositories Bundle();
  MemoryDatabase* database() { return &db_; }

 private:
  MemoryDatabase db_;
  MemoryUserRepository users_;
  MemoryShopRepository shops_;
  MemoryDishRepository dishes_;
  MemoryStockRepository stocks_;
  MemoryCartRepository carts_;
  MemoryOrderRepository orders_;
  MemoryDeliveryRepository deliveries_;
  MemoryRefundRepository refunds_;
  MemoryLogRepository logs_;
  MemoryIdempotencyRepository idempotency_;
};

}  // namespace delivery

#endif  // DELIVERY_MEMORY_REPOSITORY_H_
