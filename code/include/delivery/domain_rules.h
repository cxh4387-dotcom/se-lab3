// Copyright 2026 251220048 Cao Xuhang. All rights reserved.
// Pure domain rules: order state machine, password hashing, validation and
// the role permission matrix. No dependency on storage or on the CLI.

#ifndef DELIVERY_DOMAIN_RULES_H_
#define DELIVERY_DOMAIN_RULES_H_

#include <cstddef>
#include <string>
#include <vector>

#include "delivery/common.h"
#include "delivery/domain_types.h"

namespace delivery {
namespace domain {

// Actions that the permission matrix distinguishes. Every command exposed by
// the service layer maps to exactly one action.
enum class Action {
  kBrowseCatalog,
  kPlaceOrder,
  kCancelOwnOrder,
  kQueryOwnOrder,
  kManageShop,
  kManageDish,
  kAcceptOrReject,
  kFinishCooking,
  kAssignDelivery,
  kPickUpAndDeliver,
  kReportDeliveryFailure,
  kAuditQualification,
  kHandleRefund,
  kViewShopStatistics,
  kViewPlatformStatistics,
  kQueryAllLogs,
};

bool RoleCan(Role role, Action action);
std::string ToString(Action action);

// The nine allowed transitions of REQ-029. Everything else is rejected, which
// is what the "72 combinations" test verifies.
class OrderStateMachine {
 public:
  static bool CanTransit(OrderStatus from, OrderStatus to);
  static void EnsureTransition(OrderStatus from, OrderStatus to);
  static std::vector<OrderStatus> AllowedTargets(OrderStatus from);
  static std::size_t LegalTransitionCount();
  static const std::vector<OrderStatus>& AllStatuses();
};

// Deterministic non cryptographic hash used only for the course prototype.
// The production design uses bcrypt, documented in README.
std::string HashPassword(const std::string& raw_password);
bool VerifyPassword(const std::string& raw_password, const std::string& hash);
bool IsStrongEnoughPassword(const std::string& raw_password);

bool IsValidPhone(const std::string& phone);
void ValidatePrice(Money price);
void ValidateQuantity(int quantity);
void ValidateDishName(const std::string& name);

// Masks the middle four digits of a mobile number: 13812345678 -> 138****5678.
std::string MaskPhone(const std::string& phone);

}  // namespace domain
}  // namespace delivery

#endif  // DELIVERY_DOMAIN_RULES_H_
