// Copyright 2026 251220048 Cao Xuhang. All rights reserved.

#include <gtest/gtest.h>

#include "delivery/services.h"
#include "tests/test_fixture.h"

namespace delivery {
namespace {

using test::TestContext;

TEST(OrderServiceTest, SubmitCreatesPendingOrderWithServerTotal) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 2);
  context.cart.AddItem(world.customer, world.cheap_dish.id, 1);

  const Order order =
      context.orders.Submit(world.customer, context.Request("key-1"));

  EXPECT_EQ(order.status, OrderStatus::kPending);
  EXPECT_EQ(order.total, 1600 * 2 + 900);
  EXPECT_EQ(order.items.size(), 2u);
  EXPECT_EQ(order.RecalculateTotal(), order.total);
  EXPECT_FALSE(order.order_no.empty());
}

TEST(OrderServiceTest, SameIdempotencyKeyReturnsTheSameOrder) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 1);

  const Order first =
      context.orders.Submit(world.customer, context.Request("key-duplicate"));
  const Order second =
      context.orders.Submit(world.customer, context.Request("key-duplicate"));

  EXPECT_EQ(first.order_no, second.order_no);
  EXPECT_EQ(first.id, second.id);
  EXPECT_EQ(context.orders.ListByCustomer(world.customer).size(), 1u);
}

TEST(OrderServiceTest, SecondUnfinishedOrderForTheSameShopIsRejected) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 1);
  context.orders.Submit(world.customer, context.Request("key-a"));
  context.cart.AddItem(world.customer, world.dish.id, 1);

  EXPECT_THROW(context.orders.Submit(world.customer, context.Request("key-b")),
               DomainError);
}

TEST(OrderServiceTest, PriceSnapshotSurvivesLaterPriceChange) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 1);
  const Order order =
      context.orders.Submit(world.customer, context.Request("key-price"));

  context.catalog.ChangePrice(world.merchant, world.dish.id, 2500);
  const OrderView view = context.orders.Get(world.customer, order.order_no);

  ASSERT_EQ(view.order.items.size(), 1u);
  EXPECT_EQ(view.order.items[0].unit_price, 1600);
  EXPECT_EQ(view.order.total, 1600);
}

TEST(OrderServiceTest, RejectRegistersRefundAndReleasesStock) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 2);
  const Order order =
      context.orders.Submit(world.customer, context.Request("key-reject"));

  EXPECT_EQ(context.catalog.GetStock(world.dish.id).Remaining(), 8);
  context.orders.Reject(world.merchant, order.order_no, "原料不足");

  EXPECT_EQ(context.catalog.GetStock(world.dish.id).Remaining(), 10);
  const User admin = context.auth.Require(world.admin.id);
  const OrderView view = context.orders.Get(admin, order.order_no);
  EXPECT_EQ(view.order.status, OrderStatus::kRejected);
  ASSERT_TRUE(view.refund.has_value());
  EXPECT_EQ(view.refund->status, RefundStatus::kRefunding);
  EXPECT_EQ(view.refund->amount, order.total);
}

TEST(OrderServiceTest, RejectWithoutReasonIsRejected) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 1);
  const Order order =
      context.orders.Submit(world.customer, context.Request("key-noreason"));
  EXPECT_THROW(context.orders.Reject(world.merchant, order.order_no, ""),
               DomainError);
  EXPECT_EQ(context.orders.Get(world.merchant, order.order_no).order.status,
            OrderStatus::kPending);
}

TEST(OrderServiceTest, TimeoutCancelsPendingOrderAndRestoresStock) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 3);
  const Order order =
      context.orders.Submit(world.customer, context.Request("key-timeout"));

  context.clock.Advance(Minutes(6));
  const auto cancelled = context.orders.CancelTimeoutOrders();

  ASSERT_EQ(cancelled.size(), 1u);
  EXPECT_EQ(cancelled[0], order.order_no);
  EXPECT_EQ(context.orders.Get(world.customer, order.order_no).order.status,
            OrderStatus::kCancelled);
  EXPECT_EQ(context.catalog.GetStock(world.dish.id).Remaining(), 10);
}

TEST(OrderServiceTest, TimeoutDoesNotTouchOrderAlreadyAccepted) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 1);
  const Order order =
      context.orders.Submit(world.customer, context.Request("key-accepted"));
  context.orders.Accept(world.merchant, order.order_no);

  context.clock.Advance(Minutes(10));
  EXPECT_TRUE(context.orders.CancelTimeoutOrders().empty());
  EXPECT_EQ(context.orders.Get(context.auth.Require(world.admin.id),
                               order.order_no)
                .order.status,
            OrderStatus::kAccepted);
}

TEST(OrderServiceTest, FullHappyPathEndsCompleted) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 1);
  const Order order =
      context.orders.Submit(world.customer, context.Request("key-happy"));
  context.orders.Accept(world.merchant, order.order_no);
  context.orders.MarkReady(world.merchant, order.order_no);
  const auto task = context.delivery.Assign(world.merchant, order.order_no,
                                            world.rider.id);
  context.delivery.AcceptTask(world.rider, task.id);
  context.delivery.PickUp(world.rider, task.id);
  context.delivery.Deliver(world.rider, task.id);
  context.orders.ConfirmReceipt(world.customer, order.order_no);

  const OrderView view = context.orders.Get(world.customer, order.order_no);
  EXPECT_EQ(view.order.status, OrderStatus::kCompleted);
  ASSERT_TRUE(view.delivery.has_value());
  EXPECT_EQ(view.delivery->status, DeliveryStatus::kDelivered);

  const auto daily = context.stats.DailyOfShop(world.merchant, world.shop.id,
                                               ToDateKey(context.clock.Now()));
  EXPECT_EQ(daily.completed, 1);
  EXPECT_EQ(daily.revenue, order.total);
}

TEST(OrderServiceTest, CannotAcceptOrderOfAnotherShop) {
  TestContext context;
  const auto world = context.Seed();
  User other = context.auth.Register("13800000009", "merchant999",
                                     Role::kMerchant);
  context.auth.SubmitQualification(other.id, "91330106MA2X9Z", "", "");
  context.auth.ApproveUser(world.admin, other.id);
  other = context.auth.Require(other.id);

  context.cart.AddItem(world.customer, world.dish.id, 1);
  const Order order =
      context.orders.Submit(world.customer, context.Request("key-other"));

  EXPECT_THROW(context.orders.Accept(other, order.order_no), DomainError);
}

}  // namespace
}  // namespace delivery
