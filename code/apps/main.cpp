// Copyright 2026 251220048 Cao Xuhang. All rights reserved.
// Command line front end: a deterministic demo that walks the whole order
// lifecycle, plus an interactive menu for manual exploration.

#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "delivery/services.h"

namespace {

using delivery::AuditService;
using delivery::AuthService;
using delivery::CartService;
using delivery::CatalogService;
using delivery::Clock;
using delivery::DeliveryService;
using delivery::Dish;
using delivery::DishQuery;
using delivery::DishView;
using delivery::ErrorCode;
using delivery::FakeClock;
using delivery::IdGenerator;
using delivery::MemoryRepositories;
using delivery::Order;
using delivery::OrderService;
using delivery::OrderStatus;
using delivery::OrderView;
using delivery::RefundService;
using delivery::Repositories;
using delivery::Role;
using delivery::Shop;
using delivery::ShopBizStatus;
using delivery::StatsService;
using delivery::User;
using delivery::domain::OrderStateMachine;

void Section(const std::string& title) {
  std::cout << "\n=== " << title << " ===\n";
}

void PrintDish(const DishView& view) {
  std::cout << "  [" << view.dish.id << "] " << view.dish.name << "  "
            << delivery::FormatMoney(view.dish.price) << "  "
            << view.shop_name << "  余量 " << view.remaining << "  "
            << (view.purchasable ? "可购买" : "不可购买") << "\n";
}

void PrintOrder(const OrderView& view) {
  const Order& order = view.order;
  std::cout << "  订单 " << order.order_no << "  店铺 " << view.shop_name
            << "  状态 " << delivery::ToString(order.status) << "  金额 "
            << delivery::FormatMoney(order.total) << "\n";
  for (const auto& item : order.items) {
    std::cout << "    - " << item.dish_name << " x" << item.quantity << " @"
              << delivery::FormatMoney(item.unit_price) << "\n";
  }
  if (view.delivery.has_value()) {
    std::cout << "    配送单 " << view.delivery->id << " 状态 "
              << delivery::ToString(view.delivery->status) << " 骑手 "
              << view.delivery->rider_id << "\n";
  }
  if (view.refund.has_value()) {
    std::cout << "    退款 " << view.refund->refund_no << " "
              << delivery::ToString(view.refund->status) << " 金额 "
              << delivery::FormatMoney(view.refund->amount) << "\n";
  }
}

class App {
 public:
  explicit App(Clock* clock)
      : repos_(store_.Bundle()),
        audit_(repos_, clock, &ids_),
        auth_(repos_, clock, &ids_, &audit_),
        catalog_(repos_, clock, &ids_, &audit_),
        cart_(repos_, clock),
        refunds_(repos_, clock, &ids_, &audit_),
        orders_(repos_, clock, &ids_, &audit_, &refunds_),
        delivery_(repos_, clock, &ids_, &audit_, &orders_),
        stats_(repos_, clock),
        clock_(clock) {
    refunds_.AttachOrders(&orders_);
  }

  User Register(const std::string& phone, const std::string& password,
                Role role) {
    return auth_.Register(phone, password, role);
  }

  AuthService& auth() { return auth_; }
  CatalogService& catalog() { return catalog_; }
  CartService& cart() { return cart_; }
  OrderService& orders() { return orders_; }
  DeliveryService& delivery() { return delivery_; }
  RefundService& refunds() { return refunds_; }
  StatsService& stats() { return stats_; }
  AuditService& audit() { return audit_; }
  MemoryRepositories& store() { return store_; }
  Clock* clock() { return clock_; }

 private:
  MemoryRepositories store_;
  Repositories repos_;
  IdGenerator ids_{1000};
  AuditService audit_;
  AuthService auth_;
  CatalogService catalog_;
  CartService cart_;
  RefundService refunds_;
  OrderService orders_;
  DeliveryService delivery_;
  StatsService stats_;
  Clock* clock_;
};

void RunDemo() {
  FakeClock clock{std::chrono::system_clock::now()};
  App app(&clock);

  Section("1. 注册与资质审核");
  User customer = app.Register("13800000001", "customer123", Role::kCustomer);
  User merchant = app.Register("13800000002", "merchant123", Role::kMerchant);
  User rider = app.Register("13800000003", "rider12345", Role::kRider);
  User admin = app.Register("13800000004", "admin12345", Role::kAdmin);
  app.auth().SubmitQualification(merchant.id, "91330106MA2X8Q",
                                 "", "");
  app.auth().ApproveUser(admin, merchant.id);
  app.auth().SubmitQualification(rider.id, "", "330106199001011234",
                                 "JK-2026-7712");
  app.auth().ApproveUser(admin, rider.id);
  std::cout << "顾客 " << customer.phone << " / 商家 " << merchant.phone
            << " / 骑手 " << rider.phone << " / 管理员 " << admin.phone
            << " 已就绪\n";

  Section("2. 商家开店并上架菜品");
  Shop shop = app.catalog().CreateShop(merchant, "阿香米线（三食堂店）",
                                       "三食堂 2 楼 A12", "91330106MA2X8Q",
                                       0, 24 * 60 - 1, false);
  app.catalog().ChangeBizStatus(merchant, shop.id, ShopBizStatus::kOpen, "");
  Dish noodle = app.catalog().AddDish(merchant, shop.id, "招牌过桥米线",
                                     "面食", 1600, "大份", 10);
  Dish rice = app.catalog().AddDish(merchant, shop.id, "番茄肥牛米线",
                                    "面食", 2200, "大份", 3);
  std::cout << "店铺 " << shop.name << " 上线，菜品 2 个，库存分别为 10 与 3\n";

  Section("3. 顾客查询与加购");
  DishQuery query;
  query.keyword = "米线";
  query.sort = DishQuery::Sort::kPriceAsc;
  for (const auto& view : app.catalog().Search(query).items) {
    PrintDish(view);
  }
  app.cart().AddItem(customer, noodle.id, 2);
  app.cart().AddItem(customer, rice.id, 1);
  std::cout << "购物车：\n";
  for (const auto& line : app.cart().List(customer)) {
    std::cout << "  " << line.dish.name << " x" << line.quantity << " 单价 "
              << delivery::FormatMoney(line.current_price)
              << " 小计 " << delivery::FormatMoney(line.Subtotal()) << "\n";
  }

  Section("4. 提交订单（幂等键）");
  delivery::SubmitOrderRequest request;
  request.address = "宿舍 6 号楼 512";
  request.phone = customer.phone;
  request.idempotency_key = "req-2026-11-18-001";
  Order order = app.orders().Submit(customer, request);
  std::cout << "首次提交 -> " << order.order_no << " 金额 "
            << delivery::FormatMoney(order.total) << "\n";
  Order again = app.orders().Submit(customer, request);
  std::cout << "重复提交 -> " << again.order_no
            << "（返回同一订单，未新建）\n";

  Section("5. 商家接单与制作完成");
  app.orders().Accept(merchant, order.order_no);
  app.orders().MarkReady(merchant, order.order_no);
  PrintOrder(app.orders().Get(merchant, order.order_no));

  Section("6. 指派骑手并完成配送");
  auto task = app.delivery().Assign(merchant, order.order_no, rider.id);
  app.delivery().AcceptTask(rider, task.id);
  app.delivery().PickUp(rider, task.id);
  app.delivery().Deliver(rider, task.id);
  app.orders().ConfirmReceipt(customer, order.order_no);
  PrintOrder(app.orders().Get(customer, order.order_no));

  Section("7. 超时未接单自动取消");
  delivery::SubmitOrderRequest second;
  second.address = "宿舍 7 号楼 301";
  second.phone = customer.phone;
  second.idempotency_key = "req-2026-11-18-002";
  app.cart().AddItem(customer, rice.id, 1);
  Order pending = app.orders().Submit(customer, second);
  std::cout << "新订单 " << pending.order_no << " 进入待接单\n";
  clock.Advance(std::chrono::minutes(6));
  const auto cancelled = app.orders().CancelTimeoutOrders();
  for (const auto& no : cancelled) {
    std::cout << "超时取消：" << no << "\n";
  }
  PrintOrder(app.orders().Get(customer, pending.order_no));

  Section("8. 退款异常与重试");
  auto refund = app.refunds().FindByOrder(pending.id);
  if (refund.has_value()) {
    app.refunds().Retry(admin, refund->id, false);
    std::cout << "第一次线下退款失败，退款单状态："
              << delivery::ToString(
                     app.refunds().FindByOrder(pending.id)->status)
              << "\n";
    app.refunds().Retry(admin, refund->id, true);
    PrintOrder(app.orders().Get(customer, pending.order_no));
  }

  Section("9. 统计汇总");
  const int today = delivery::ToDateKey(clock.Now());
  const auto daily = app.stats().DailyOfShop(merchant, shop.id, today);
  std::cout << "本店当日：订单 " << daily.order_count << " 单，已完成 "
            << daily.completed << " 单，营业额 "
            << delivery::FormatMoney(daily.revenue) << "，超时率 "
            << std::fixed << std::setprecision(1) << daily.timeout_rate * 100
            << "%\n";
  for (const auto& row : app.stats().DishSales(shop.id, today, today)) {
    std::cout << "  菜品 " << row.dish_name << " 销量 " << row.quantity
              << " 销售额 " << delivery::FormatMoney(row.revenue) << "\n";
  }
  for (const auto& row : app.stats().PlatformRank(today)) {
    std::cout << "  平台排名 " << row.shop_name << " 营业额 "
              << delivery::FormatMoney(row.revenue) << "\n";
  }

  Section("10. 操作日志（最近 6 条）");
  const auto logs = app.audit().Query(admin, std::nullopt, std::nullopt,
                                      std::nullopt, std::nullopt);
  const std::size_t begin = logs.size() > 6 ? logs.size() - 6 : 0;
  for (std::size_t i = begin; i < logs.size(); ++i) {
    std::cout << "  " << delivery::FormatTime(logs[i].created_at) << " "
              << delivery::ToString(logs[i].actor_role) << " "
              << logs[i].action << " " << logs[i].object_type << " "
              << logs[i].object_id << " " << logs[i].before_state << " -> "
              << logs[i].after_state << "\n";
  }

  Section("11. 非法状态转换被拒绝");
  try {
    app.orders().MarkReady(merchant, order.order_no);
  } catch (const delivery::DomainError& error) {
    std::cout << "拒绝：" << error.what() << "（错误码 "
              << static_cast<int>(error.code()) << "）\n";
  }

  std::cout << "\n演示结束。加 --interactive 可进入交互菜单。\n";
}

void RunInteractive() {
  FakeClock clock{std::chrono::system_clock::now()};
  App app(&clock);
  User customer = app.Register("13900000001", "customer123", Role::kCustomer);
  User merchant = app.Register("13900000002", "merchant123", Role::kMerchant);
  User rider = app.Register("13900000003", "rider12345", Role::kRider);
  User admin = app.Register("13900000004", "admin12345", Role::kAdmin);
  app.auth().SubmitQualification(merchant.id, "91330106MA2X8Q", "", "");
  app.auth().ApproveUser(admin, merchant.id);
  app.auth().SubmitQualification(rider.id, "", "330106199001011234",
                                 "JK-2026-7712");
  app.auth().ApproveUser(admin, rider.id);
  Shop shop = app.catalog().CreateShop(merchant, "交互演示店铺", "教学区 1 号",
                                       "91330106MA2X8Q", 0, 24 * 60 - 1,
                                       false);
  app.catalog().ChangeBizStatus(merchant, shop.id, ShopBizStatus::kOpen, "");
  app.catalog().AddDish(merchant, shop.id, "示例米线", "面食", 1500, "标准",
                        20);

  bool running = true;
  while (running) {
    std::cout << "\n1 浏览菜品  2 加入购物车  3 查看购物车  4 下单\n"
                 "5 商家接单  6 制作完成  7 指派骑手  8 骑手取餐\n"
                 "9 骑手送达  10 确认收货  11 查看订单  12 运行调度\n"
                 "13 查看统计  0 退出\n选择：";
    int choice = 0;
    if (!(std::cin >> choice)) {
      break;
    }
    try {
      switch (choice) {
        case 1: {
          DishQuery query;
          for (const auto& view : app.catalog().Search(query).items) {
            PrintDish(view);
          }
          break;
        }
        case 2: {
          std::int64_t dish_id = 0;
          int quantity = 1;
          std::cout << "菜品编号 数量：";
          std::cin >> dish_id >> quantity;
          app.cart().AddItem(customer, dish_id, quantity);
          std::cout << "已加入购物车\n";
          break;
        }
        case 3:
          for (const auto& line : app.cart().List(customer)) {
            std::cout << "  " << line.dish.name << " x" << line.quantity
                      << " 小计 " << delivery::FormatMoney(line.Subtotal())
                      << "\n";
          }
          break;
        case 4: {
          delivery::SubmitOrderRequest request;
          request.address = "宿舍 6 号楼 512";
          request.phone = customer.phone;
          request.idempotency_key =
              "interactive-" + std::to_string(clock.Now().time_since_epoch()
                                                  .count());
          PrintOrder(app.orders().Get(customer,
                                      app.orders().Submit(customer, request)
                                          .order_no));
          break;
        }
        case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 13: {
          std::string order_no;
          std::cout << "订单号：";
          std::cin >> order_no;
          if (choice == 5) {
            app.orders().Accept(merchant, order_no);
          } else if (choice == 6) {
            app.orders().MarkReady(merchant, order_no);
          } else if (choice == 7) {
            const OrderView view = app.orders().Get(merchant, order_no);
            app.delivery().Assign(merchant, order_no, rider.id);
            std::cout << "已指派骑手 " << rider.id << "\n";
            (void)view;
          } else if (choice == 8 || choice == 9) {
            const auto task = app.delivery().ListByRider(rider);
            if (task.empty()) {
              std::cout << "暂无配送任务\n";
              break;
            }
            if (choice == 8) {
              app.delivery().AcceptTask(rider, task.front().id);
              app.delivery().PickUp(rider, task.front().id);
            } else {
              app.delivery().Deliver(rider, task.front().id);
            }
          } else if (choice == 10) {
            app.orders().ConfirmReceipt(customer, order_no);
          } else if (choice == 11) {
            PrintOrder(app.orders().Get(customer, order_no));
          } else {
            const auto daily = app.stats().DailyOfShop(
                merchant, shop.id, delivery::ToDateKey(clock.Now()));
            std::cout << "当日订单 " << daily.order_count << " 营业额 "
                      << delivery::FormatMoney(daily.revenue) << "\n";
          }
          break;
        }
        case 12: {
          const auto cancelled = app.orders().CancelTimeoutOrders();
          std::cout << "调度器取消 " << cancelled.size() << " 笔超时订单\n";
          break;
        }
        case 0:
          running = false;
          break;
        default:
          std::cout << "无效选项\n";
      }
    } catch (const delivery::DomainError& error) {
      std::cout << "操作失败：" << error.what() << "\n";
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
  const std::string mode = argc > 1 ? argv[1] : "--demo";
  try {
    if (mode == "--interactive") {
      RunInteractive();
    } else {
      RunDemo();
    }
  } catch (const delivery::DomainError& error) {
    std::cout << "运行中断：" << error.what() << "（错误码 "
              << static_cast<int>(error.code()) << "）\n";
    return 1;
  }
  return 0;
}
