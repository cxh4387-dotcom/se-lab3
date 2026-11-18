// Copyright 2026 251220048 Cao Xuhang. All rights reserved.
// Access control: every command must verify the role and the data ownership,
// not only the menu that the caller happens to see.

#include <gtest/gtest.h>

#include "delivery/services.h"
#include "tests/test_fixture.h"

namespace delivery {
namespace {

using test::TestContext;

TEST(AccessControlTest, CustomerCannotDriveTheKitchen) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 1);
  const Order order =
      context.orders.Submit(world.customer, context.Request("key-acl-1"));

  EXPECT_THROW(context.orders.Accept(world.customer, order.order_no),
               DomainError);
  EXPECT_THROW(context.orders.MarkReady(world.customer, order.order_no),
               DomainError);
  EXPECT_THROW(context.delivery.Assign(world.customer, order.order_no,
                                       world.rider.id),
               DomainError);
}

TEST(AccessControlTest, AdminCannotPlaceAnOrder) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 1);
  EXPECT_THROW(context.orders.Submit(world.admin, context.Request("key-acl-2")),
               DomainError);
}

TEST(AccessControlTest, MerchantCannotAuditQualification) {
  TestContext context;
  const auto world = context.Seed();
  User newcomer = context.auth.Register("13800000008", "rider12345",
                                       Role::kRider);
  EXPECT_THROW(context.auth.ApproveUser(world.merchant, newcomer.id),
               DomainError);
}

TEST(AccessControlTest, RiderCannotTouchAnotherRidersTask) {
  TestContext context;
  const auto world = context.Seed();
  User other_rider = context.auth.Register("13800000007", "rider12345",
                                           Role::kRider);
  context.auth.SubmitQualification(other_rider.id, "",
                                   "330106199001015555", "JK-2026-8888");
  context.auth.ApproveUser(world.admin, other_rider.id);
  other_rider = context.auth.Require(other_rider.id);

  context.cart.AddItem(world.customer, world.dish.id, 1);
  const Order order =
      context.orders.Submit(world.customer, context.Request("key-acl-3"));
  context.orders.Accept(world.merchant, order.order_no);
  context.orders.MarkReady(world.merchant, order.order_no);
  const auto task = context.delivery.Assign(world.merchant, order.order_no,
                                            world.rider.id);

  EXPECT_THROW(context.delivery.AcceptTask(other_rider, task.id), DomainError);
  EXPECT_THROW(context.delivery.Deliver(other_rider, task.id), DomainError);
}

TEST(AccessControlTest, MerchantCannotReadAnotherShopsOrders) {
  TestContext context;
  const auto world = context.Seed();
  User other = context.auth.Register("13800000006", "merchant999",
                                     Role::kMerchant);
  context.auth.SubmitQualification(other.id, "91330106MA2X7Y", "", "");
  context.auth.ApproveUser(world.admin, other.id);
  other = context.auth.Require(other.id);

  context.cart.AddItem(world.customer, world.dish.id, 1);
  context.orders.Submit(world.customer, context.Request("key-acl-4"));
  EXPECT_THROW(context.orders.ListByShop(other, world.shop.id), DomainError);
  EXPECT_THROW(context.stats.DailyOfShop(other, world.shop.id,
                                         ToDateKey(context.clock.Now())),
               DomainError);
}

TEST(AccessControlTest, RiderMustGiveAReasonToReportFailure) {
  TestContext context;
  const auto world = context.Seed();
  context.cart.AddItem(world.customer, world.dish.id, 1);
  const Order order =
      context.orders.Submit(world.customer, context.Request("key-acl-5"));
  context.orders.Accept(world.merchant, order.order_no);
  context.orders.MarkReady(world.merchant, order.order_no);
  const auto task = context.delivery.Assign(world.merchant, order.order_no,
                                            world.rider.id);
  context.delivery.AcceptTask(world.rider, task.id);

  EXPECT_THROW(context.delivery.ReportFailure(world.rider, task.id, ""),
               DomainError);
  EXPECT_EQ(context.orders.Get(world.merchant, order.order_no).order.status,
            OrderStatus::kDelivering);
}

TEST(AccessControlTest, LockedAccountCannotLogin) {
  TestContext context;
  context.auth.Register("13800000005", "customer123", Role::kCustomer);
  for (int attempt = 0; attempt < 5; ++attempt) {
    EXPECT_THROW(context.auth.Login("13800000005", "wrong-password"),
                 DomainError);
  }
  EXPECT_THROW(context.auth.Login("13800000005", "customer123"), DomainError);
  context.clock.Advance(Minutes(11));
  EXPECT_NO_THROW(context.auth.Login("13800000005", "customer123"));
}

}  // namespace
}  // namespace delivery
