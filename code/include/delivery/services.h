// Copyright 2026 251220048 Cao Xuhang. All rights reserved.
// Application services. Each service implements one use case group of the
// design report and depends only on repository interfaces, a clock and an id
// generator, which keeps it unit testable.

#ifndef DELIVERY_SERVICES_H_
#define DELIVERY_SERVICES_H_

#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "delivery/common.h"
#include "delivery/domain_rules.h"
#include "delivery/domain_types.h"
#include "delivery/memory_repository.h"
#include "delivery/repositories.h"

namespace delivery {

class AuditService {
 public:
  AuditService(const Repositories& repos, Clock* clock, IdGenerator* ids);

  void Record(const User& actor, const std::string& action,
              const std::string& object_type, const std::string& object_id,
              const std::string& before_state, const std::string& after_state,
              const std::string& result, const std::string& source_ip = "");

  std::vector<LogEntry> Query(const User& actor,
                              const std::optional<std::string>& action,
                              const std::optional<std::string>& object_id,
                              const std::optional<TimePoint>& from,
                              const std::optional<TimePoint>& to) const;

  std::size_t Count() const;

 private:
  Repositories repos_;
  Clock* clock_;
  IdGenerator* ids_;
};

class AuthService {
 public:
  AuthService(const Repositories& repos, Clock* clock, IdGenerator* ids,
              AuditService* audit);

  User Register(const std::string& phone, const std::string& password,
                Role role);
  User Login(const std::string& phone, const std::string& password);

  // Merchant submits shop data, rider submits id card and health certificate.
  void SubmitQualification(UserId user_id, const std::string& license_no,
                           const std::string& id_card_no,
                           const std::string& health_no);
  void ApproveUser(const User& admin, UserId user_id);
  void RejectUser(const User& admin, UserId user_id,
                  const std::string& reason);

  User Require(UserId user_id) const;
  void EnsureCan(const User& user, domain::Action action) const;

 private:
  Repositories repos_;
  Clock* clock_;
  IdGenerator* ids_;
  AuditService* audit_;
};

struct DishQuery {
  std::string keyword;
  std::string category;
  Money min_price = -1;
  Money max_price = -1;
  bool only_open_shop = true;
  enum class Sort { kSales, kPriceAsc, kPriceDesc } sort = Sort::kSales;
  int page = 1;
  int page_size = 20;
};

struct DishView {
  Dish dish;
  std::string shop_name;
  int remaining = 0;
  bool purchasable = false;
};

struct PageResult {
  std::vector<DishView> items;
  int total = 0;
  int page = 1;
  int page_size = 20;
};

struct CartLine {
  Dish dish;
  int quantity = 0;
  Money current_price = 0;
  Money snapshot_price = 0;
  bool price_changed = false;
  bool sold_out = false;
  Money Subtotal() const { return current_price * quantity; }
};

class CatalogService {
 public:
  CatalogService(const Repositories& repos, Clock* clock, IdGenerator* ids,
                 AuditService* audit);

  Shop CreateShop(const User& merchant, const std::string& name,
                  const std::string& address, const std::string& license_no,
                  int open_minute, int close_minute, bool close_next_day);
  void ChangeBizStatus(const User& merchant, ShopId shop_id,
                       ShopBizStatus status, const std::string& reason);
  void ForceClose(const User& admin, ShopId shop_id,
                  const std::string& reason);
  void RefreshBizStatus(const Shop& shop);

  Dish AddDish(const User& merchant, ShopId shop_id, const std::string& name,
               const std::string& category, Money price,
               const std::string& spec, int daily_qty);
  void UpdateDish(const User& merchant, DishId dish_id,
                  const std::string& name, const std::string& category,
                  const std::string& spec);
  void ChangePrice(const User& merchant, DishId dish_id, Money new_price);
  void ChangeDishStatus(const User& merchant, DishId dish_id,
                        DishStatus status);
  void ReplenishStock(const User& merchant, DishId dish_id, int extra_qty);
  void ResetDailyStock(const User& merchant, DishId dish_id, int total_qty);

  PageResult Search(const DishQuery& query) const;
  std::vector<DishView> ListShopDishes(ShopId shop_id) const;
  Dish GetDish(DishId dish_id) const;
  Shop GetShop(ShopId shop_id) const;
  DailyStock GetStock(DishId dish_id) const;

  bool ShopAcceptsOrder(ShopId shop_id) const;

 private:
  void EnsureShopOwner(const User& merchant, ShopId shop_id) const;
  void EnsureCan(const User& user, domain::Action action) const;
  int RemainingOf(DishId dish_id) const;

  Repositories repos_;
  Clock* clock_;
  IdGenerator* ids_;
  AuditService* audit_;
};

class CartService {
 public:
  CartService(const Repositories& repos, Clock* clock);

  void AddItem(const User& customer, DishId dish_id, int quantity);
  void UpdateQuantity(const User& customer, DishId dish_id,
                      const std::string& spec, int quantity);
  void RemoveItem(const User& customer, DishId dish_id,
                  const std::string& spec);
  void Clear(const User& customer);
  std::vector<CartLine> List(const User& customer) const;
  bool HasPriceChange(const User& customer) const;

 private:
  Repositories repos_;
  Clock* clock_;
};

struct SubmitOrderRequest {
  std::string address;
  std::string phone;
  std::string idempotency_key;
};

struct OrderView {
  Order order;
  std::string shop_name;
  std::optional<DeliveryTask> delivery;
  std::optional<Refund> refund;
};

class RefundService;

class OrderService {
 public:
  OrderService(const Repositories& repos, Clock* clock, IdGenerator* ids,
               AuditService* audit, RefundService* refunds);
  ~OrderService();

  Order Submit(const User& customer, const SubmitOrderRequest& request);
  void Accept(const User& merchant, const std::string& order_no);
  void Reject(const User& merchant, const std::string& order_no,
              const std::string& reason);
  void MarkReady(const User& merchant, const std::string& order_no);
  void CancelByCustomer(const User& customer, const std::string& order_no);
  void CancelByMerchant(const User& merchant, const std::string& order_no);
  void ConfirmReceipt(const User& customer, const std::string& order_no);

  // Scheduler entry: cancels every pending order older than five minutes.
  std::vector<std::string> CancelTimeoutOrders();

  OrderView Get(const User& actor, const std::string& order_no) const;
  std::vector<OrderView> ListByCustomer(const User& customer) const;
  std::vector<OrderView> ListByShop(const User& merchant,
                                    ShopId shop_id) const;

  // Used by the delivery module when a rider reports a failure.
  void MarkDeliveryFailed(OrderId order_id, const std::string& reason,
                          const User& actor);
  void MarkRefunded(OrderId order_id, const User& actor);

  OrderRepository& repository() { return repos_.orders; }

 private:
  Order LoadForUpdate(const std::string& order_no);
  void PersistTransition(Order* order, OrderStatus target,
                         const std::string& reason, const User& actor);
  void ReleaseStock(const Order& order);

  Repositories repos_;
  Clock* clock_;
  IdGenerator* ids_;
  AuditService* audit_;
  RefundService* refunds_;
  // Guards the check-then-create section of Submit and the released set.
  std::mutex submit_mutex_;
  std::set<OrderId> released_orders_;
};

class DeliveryService {
 public:
  DeliveryService(const Repositories& repos, Clock* clock, IdGenerator* ids,
                  AuditService* audit, OrderService* orders);

  DeliveryTask Assign(const User& actor, const std::string& order_no,
                      UserId rider_id);
  void AcceptTask(const User& rider, TaskId task_id);
  void PickUp(const User& rider, TaskId task_id);
  void Deliver(const User& rider, TaskId task_id);
  void ReportFailure(const User& rider, TaskId task_id,
                     const std::string& reason);
  int AutoConfirmDelivered(int after_minutes);
  DeliveryTask Reassign(const User& admin, TaskId task_id, UserId rider_id);

  std::vector<DeliveryTask> ListByRider(const User& rider) const;

 private:
  DeliveryTask LoadTask(TaskId task_id) const;
  void EnsureRiderOwns(const User& rider, const DeliveryTask& task) const;

  Repositories repos_;
  Clock* clock_;
  IdGenerator* ids_;
  AuditService* audit_;
  OrderService* orders_;
};

class RefundService {
 public:
  RefundService(const Repositories& repos, Clock* clock, IdGenerator* ids,
                AuditService* audit);

  Refund Create(const User& actor, OrderId order_id, Money amount,
                const std::string& trigger);
  // Simulates the offline refund. A failure must not move the order to
  // "refunded"; it becomes "refund failed" and can be retried.
  void Retry(const User& admin, RefundId refund_id, bool offline_ok);
  void MarkSucceeded(RefundId refund_id, const User& actor);
  std::optional<Refund> FindByOrder(OrderId order_id) const;
  std::vector<Refund> ListFailed() const;

  // Wired after construction to avoid a constructor cycle: a successful
  // refund must move the order to "refunded".
  void AttachOrders(OrderService* orders) { orders_ = orders; }

 private:
  Repositories repos_;
  Clock* clock_;
  IdGenerator* ids_;
  AuditService* audit_;
  OrderService* orders_ = nullptr;
};

struct ShopDailyStats {
  ShopId shop_id = 0;
  int biz_date = 0;
  Money revenue = 0;
  int order_count = 0;
  int completed = 0;
  int cancelled = 0;
  int rejected = 0;
  int delivery_failed = 0;
  double timeout_rate = 0.0;
  double failure_rate = 0.0;
};

struct DishSalesRow {
  DishId dish_id = 0;
  std::string dish_name;
  int quantity = 0;
  Money revenue = 0;
};

struct ShopRankRow {
  ShopId shop_id = 0;
  std::string shop_name;
  Money revenue = 0;
  int order_count = 0;
};

class StatsService {
 public:
  StatsService(const Repositories& repos, Clock* clock);

  ShopDailyStats DailyOfShop(const User& merchant, ShopId shop_id,
                             int biz_date) const;
  std::vector<DishSalesRow> DishSales(ShopId shop_id, int from_date,
                                      int to_date) const;
  std::vector<ShopRankRow> PlatformRank(int biz_date) const;

 private:
  Repositories repos_;
  Clock* clock_;
};

}  // namespace delivery

#endif  // DELIVERY_SERVICES_H_
