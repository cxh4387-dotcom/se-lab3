// Copyright 2026 251220048 Cao Xuhang. All rights reserved.
// Refund consistency: a failed offline refund must never make the order look
// refunded, and a retry must be idempotent.

#include <gtest/gtest.h>

#include "delivery/services.h"
#include "tests/test_fixture.h"

namespace delivery {
namespace {

using test::TestContext;

TEST(RefundTest, CancellationCreatesRefundingRecord) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 2);
  const Order order =
      context.orders.Submit(world.customer, context.Request("key-refund-1"));
  context.orders.CancelByCustomer(world.customer, order.order_no);

  const auto refund = context.refunds.FindByOrder(order.id);
  ASSERT_TRUE(refund.has_value());
  EXPECT_EQ(refund->status, RefundStatus::kRefunding);
  EXPECT_EQ(refund->amount, order.total);
}

TEST(RefundTest, FailedRefundKeepsOrderCancelled) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 1);
  const Order order =
      context.orders.Submit(world.customer, context.Request("key-refund-2"));
  context.orders.CancelByCustomer(world.customer, order.order_no);
  const auto refund = context.refunds.FindByOrder(order.id);
  ASSERT_TRUE(refund.has_value());

  context.refunds.Retry(world.admin, refund->id, false);

  const auto after = context.refunds.FindByOrder(order.id);
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(after->status, RefundStatus::kFailed);
  EXPECT_EQ(after->retry_count, 1);
  EXPECT_EQ(context.orders.Get(world.admin, order.order_no).order.status,
            OrderStatus::kCancelled);
}

TEST(RefundTest, SuccessfulRetryMarksOrderRefunded) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 1);
  const Order order =
      context.orders.Submit(world.customer, context.Request("key-refund-3"));
  context.orders.CancelByCustomer(world.customer, order.order_no);
  const auto refund = context.refunds.FindByOrder(order.id);
  ASSERT_TRUE(refund.has_value());

  context.refunds.Retry(world.admin, refund->id, false);
  context.refunds.Retry(world.admin, refund->id, true);

  EXPECT_EQ(context.refunds.FindByOrder(order.id)->status,
            RefundStatus::kRefunded);
  EXPECT_EQ(context.orders.Get(world.admin, order.order_no).order.status,
            OrderStatus::kRefunded);
}

TEST(RefundTest, CannotRefundTwice) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 1);
  const Order order =
      context.orders.Submit(world.customer, context.Request("key-refund-4"));
  context.orders.CancelByCustomer(world.customer, order.order_no);
  const auto refund = context.refunds.FindByOrder(order.id);
  ASSERT_TRUE(refund.has_value());

  context.refunds.Retry(world.admin, refund->id, true);
  EXPECT_THROW(context.refunds.Retry(world.admin, refund->id, true),
               DomainError);
  EXPECT_THROW(context.refunds.MarkSucceeded(refund->id, world.admin),
               DomainError);
}

TEST(RefundTest, OnlyAdminCanRetryRefund) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 1);
  const Order order =
      context.orders.Submit(world.customer, context.Request("key-refund-5"));
  context.orders.CancelByCustomer(world.customer, order.order_no);
  const auto refund = context.refunds.FindByOrder(order.id);
  ASSERT_TRUE(refund.has_value());

  EXPECT_THROW(context.refunds.Retry(world.customer, refund->id, true),
               DomainError);
  EXPECT_EQ(context.refunds.FindByOrder(order.id)->status,
            RefundStatus::kRefunding);
}

TEST(RefundTest, FailedRefundsAreListedForTheAdminQueue) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 1);
  const Order order =
      context.orders.Submit(world.customer, context.Request("key-refund-6"));
  context.orders.CancelByCustomer(world.customer, order.order_no);
  const auto refund = context.refunds.FindByOrder(order.id);
  context.refunds.Retry(world.admin, refund->id, false);

  const auto failed = context.refunds.ListFailed();
  ASSERT_EQ(failed.size(), 1u);
  EXPECT_EQ(failed[0].order_id, order.id);
}

}  // namespace
}  // namespace delivery
