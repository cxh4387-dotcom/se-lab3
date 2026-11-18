// Copyright 2026 251220048 Cao Xuhang. All rights reserved.
// Concurrency tests: the stock must never be oversold even when one hundred
// requests arrive at the same time.

#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "delivery/services.h"
#include "tests/test_fixture.h"

namespace delivery {
namespace {

using test::TestContext;

TEST(InventoryTest, TryDeductIsAtomicUnderContention) {
  TestContext context;
  const auto world = context.Seed();
  const int biz_date = ToDateKey(context.clock.Now());

  std::atomic<int> success{0};
  std::vector<std::thread> workers;
  workers.reserve(100);
  for (int i = 0; i < 100; ++i) {
    workers.emplace_back([&context, &success, &world, biz_date]() {
      if (context.repos.stocks.TryDeduct(world.dish.id, biz_date, 1)) {
        success.fetch_add(1);
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }

  EXPECT_EQ(success.load(), 10);
  EXPECT_EQ(context.catalog.GetStock(world.dish.id).Remaining(), 0);
}

TEST(InventoryTest, HundredConcurrentOrdersOnlySellAvailableStock) {
  TestContext context;
  const auto world = context.Seed();

  // Prepare one hundred customers with one item each, single threaded.
  std::vector<User> customers;
  customers.reserve(100);
  for (int i = 0; i < 100; ++i) {
    const std::string phone = "1390000" + std::to_string(1000 + i);
    User customer = context.auth.Register(phone, "customer123",
                                          Role::kCustomer);
    context.cart.AddItem(customer, world.dish.id, 1);
    customers.push_back(customer);
  }
  EXPECT_EQ(context.catalog.GetStock(world.dish.id).Remaining(), 10);

  std::atomic<int> accepted{0};
  std::atomic<int> rejected{0};
  std::vector<std::thread> workers;
  workers.reserve(customers.size());
  for (std::size_t i = 0; i < customers.size(); ++i) {
    workers.emplace_back([&context, &customers, &accepted, &rejected, i]() {
      try {
        SubmitOrderRequest request;
        request.address = "宿舍 6 号楼 512";
        request.phone = customers[i].phone;
        request.idempotency_key = "concurrent-" + std::to_string(i);
        context.orders.Submit(customers[i], request);
        accepted.fetch_add(1);
      } catch (const DomainError&) {
        rejected.fetch_add(1);
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }

  EXPECT_EQ(accepted.load(), 10);
  EXPECT_EQ(rejected.load(), 90);
  const auto stock = context.catalog.GetStock(world.dish.id);
  EXPECT_EQ(stock.Remaining(), 0);
  EXPECT_EQ(stock.sold_qty, 10);
}

TEST(InventoryTest, ReleaseIsIdempotentPerOrder) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 3);
  const Order order =
      context.orders.Submit(world.customer, context.Request("key-release"));
  EXPECT_EQ(context.catalog.GetStock(world.dish.id).Remaining(), 7);

  context.orders.CancelByCustomer(world.customer, order.order_no);
  EXPECT_EQ(context.catalog.GetStock(world.dish.id).Remaining(), 10);
  // A second cancellation is rejected by the state machine and must not
  // release the stock a second time.
  EXPECT_THROW(context.orders.CancelByCustomer(world.customer, order.order_no),
               DomainError);
  EXPECT_EQ(context.catalog.GetStock(world.dish.id).Remaining(), 10);
}

TEST(InventoryTest, SoldOutDishCannotBeAddedAgain) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.cheap_dish.id, 3);
  EXPECT_THROW(context.cart.AddItem(world.customer, world.cheap_dish.id, 1),
               DomainError);
}

}  // namespace
}  // namespace delivery
