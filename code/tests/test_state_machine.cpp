// Copyright 2026 251220048 Cao Xuhang. All rights reserved.

#include <gtest/gtest.h>

#include <vector>

#include "delivery/domain_rules.h"

namespace delivery {
namespace {

using domain::OrderStateMachine;

TEST(OrderStateMachineTest, AllowsExactlyTheDocumentedTransitions) {
  // Every pair that the design report lists as legal.
  const std::vector<std::pair<OrderStatus, OrderStatus>> legal = {
      {OrderStatus::kPending, OrderStatus::kAccepted},
      {OrderStatus::kPending, OrderStatus::kRejected},
      {OrderStatus::kPending, OrderStatus::kCancelled},
      {OrderStatus::kAccepted, OrderStatus::kReady},
      {OrderStatus::kAccepted, OrderStatus::kCancelled},
      {OrderStatus::kReady, OrderStatus::kDelivering},
      {OrderStatus::kDelivering, OrderStatus::kCompleted},
      {OrderStatus::kDelivering, OrderStatus::kDeliveryFailed},
      {OrderStatus::kDeliveryFailed, OrderStatus::kDelivering},
      {OrderStatus::kDeliveryFailed, OrderStatus::kRefunded},
      {OrderStatus::kRejected, OrderStatus::kRefunded},
      {OrderStatus::kCancelled, OrderStatus::kRefunded},
  };
  for (const auto& edge : legal) {
    EXPECT_TRUE(OrderStateMachine::CanTransit(edge.first, edge.second))
        << ToString(edge.first) << " -> " << ToString(edge.second);
  }
  EXPECT_EQ(OrderStateMachine::LegalTransitionCount(), legal.size());
}

TEST(OrderStateMachineTest, RejectsEveryOtherCombination) {
  const auto& statuses = OrderStateMachine::AllStatuses();
  ASSERT_EQ(statuses.size(), 9u);
  int legal_seen = 0;
  for (OrderStatus from : statuses) {
    for (OrderStatus to : statuses) {
      if (from == to) {
        continue;
      }
      if (OrderStateMachine::CanTransit(from, to)) {
        ++legal_seen;
      }
    }
  }
  EXPECT_EQ(legal_seen, 12);
  // 9 * 8 = 72 ordered pairs, 12 of them legal.
  EXPECT_EQ(static_cast<int>(statuses.size()) * 8 - 12, 60);
}

TEST(OrderStateMachineTest, TerminalStatesHaveNoOutgoingEdge) {
  EXPECT_TRUE(
      OrderStateMachine::AllowedTargets(OrderStatus::kCompleted).empty());
  EXPECT_TRUE(
      OrderStateMachine::AllowedTargets(OrderStatus::kRefunded).empty());
}

TEST(OrderStateMachineTest, EnsureTransitionThrowsConflict) {
  EXPECT_THROW(OrderStateMachine::EnsureTransition(OrderStatus::kCompleted,
                                                   OrderStatus::kCancelled),
               DomainError);
  try {
    OrderStateMachine::EnsureTransition(OrderStatus::kReady,
                                        OrderStatus::kPending);
    FAIL() << "expected DomainError";
  } catch (const DomainError& error) {
    EXPECT_EQ(error.code(), ErrorCode::kStateChanged);
  }
}

TEST(OrderStateMachineTest, ForbiddenShortcutsAreRejected) {
  // The three shortcuts the design report explicitly forbids.
  EXPECT_FALSE(OrderStateMachine::CanTransit(OrderStatus::kPending,
                                             OrderStatus::kDelivering));
  EXPECT_FALSE(OrderStateMachine::CanTransit(OrderStatus::kCompleted,
                                             OrderStatus::kCancelled));
  EXPECT_FALSE(OrderStateMachine::CanTransit(OrderStatus::kDelivering,
                                             OrderStatus::kCancelled));
  EXPECT_FALSE(OrderStateMachine::CanTransit(OrderStatus::kRefunded,
                                             OrderStatus::kPending));
}

TEST(PermissionMatrixTest, RolesAreLimitedToTheirOwnActions) {
  EXPECT_TRUE(domain::RoleCan(Role::kCustomer, domain::Action::kPlaceOrder));
  EXPECT_FALSE(domain::RoleCan(Role::kCustomer,
                               domain::Action::kManageShop));
  EXPECT_FALSE(domain::RoleCan(Role::kCustomer,
                               domain::Action::kAuditQualification));
  EXPECT_TRUE(domain::RoleCan(Role::kMerchant,
                              domain::Action::kAcceptOrReject));
  EXPECT_FALSE(domain::RoleCan(Role::kMerchant,
                               domain::Action::kPickUpAndDeliver));
  EXPECT_TRUE(domain::RoleCan(Role::kRider,
                              domain::Action::kReportDeliveryFailure));
  EXPECT_FALSE(domain::RoleCan(Role::kRider, domain::Action::kHandleRefund));
  EXPECT_TRUE(domain::RoleCan(Role::kAdmin,
                              domain::Action::kAuditQualification));
  EXPECT_FALSE(domain::RoleCan(Role::kAdmin, domain::Action::kPlaceOrder));
}

TEST(ValidationTest, RejectsBadPhoneAndPrice) {
  EXPECT_TRUE(domain::IsValidPhone("13800000001"));
  EXPECT_FALSE(domain::IsValidPhone("1380000000"));
  EXPECT_FALSE(domain::IsValidPhone("23800000001"));
  EXPECT_THROW(domain::ValidatePrice(0), DomainError);
  EXPECT_THROW(domain::ValidatePrice(-100), DomainError);
  EXPECT_NO_THROW(domain::ValidatePrice(1));
  EXPECT_EQ(domain::MaskPhone("13812345678"), "138****5678");
}

}  // namespace
}  // namespace delivery
