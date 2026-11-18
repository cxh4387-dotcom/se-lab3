// Copyright 2026 251220048 Cao Xuhang. All rights reserved.

#include "delivery/domain_rules.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace delivery {
namespace domain {
namespace {

// The whitelist of REQ-029. Every edge carries the role that may trigger it,
// which is why the state machine alone is not enough for authorisation: the
// service layer checks the role before asking for the transition.
const std::map<OrderStatus, std::vector<OrderStatus>>& TransitionTable() {
  static const std::map<OrderStatus, std::vector<OrderStatus>> table = {
      {OrderStatus::kPending,
       {OrderStatus::kAccepted, OrderStatus::kRejected,
        OrderStatus::kCancelled}},
      {OrderStatus::kAccepted,
       {OrderStatus::kReady, OrderStatus::kCancelled}},
      {OrderStatus::kReady, {OrderStatus::kDelivering}},
      {OrderStatus::kDelivering,
       {OrderStatus::kCompleted, OrderStatus::kDeliveryFailed}},
      {OrderStatus::kDeliveryFailed,
       {OrderStatus::kDelivering, OrderStatus::kRefunded}},
      {OrderStatus::kRejected, {OrderStatus::kRefunded}},
      {OrderStatus::kCancelled, {OrderStatus::kRefunded}},
      {OrderStatus::kCompleted, {}},
      {OrderStatus::kRefunded, {}},
  };
  return table;
}

std::uint64_t Fnv1a(const std::string& text) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (unsigned char c : text) {
    hash ^= static_cast<std::uint64_t>(c);
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::string ToHex(std::uint64_t value) {
  std::ostringstream stream;
  stream << std::hex << std::setw(16) << std::setfill('0') << value;
  return stream.str();
}

}  // namespace

bool RoleCan(Role role, Action action) {
  static const std::map<Role, std::set<Action>> matrix = {
      {Role::kCustomer,
       {Action::kBrowseCatalog, Action::kPlaceOrder,
        Action::kCancelOwnOrder, Action::kQueryOwnOrder}},
      {Role::kMerchant,
       {Action::kBrowseCatalog, Action::kQueryOwnOrder,
        Action::kManageShop, Action::kManageDish, Action::kAcceptOrReject,
        Action::kFinishCooking, Action::kAssignDelivery,
        Action::kViewShopStatistics}},
      {Role::kRider,
       {Action::kBrowseCatalog, Action::kPickUpAndDeliver,
        Action::kReportDeliveryFailure}},
      {Role::kAdmin,
       {Action::kBrowseCatalog, Action::kQueryOwnOrder,
        Action::kAssignDelivery, Action::kAuditQualification,
        Action::kHandleRefund, Action::kViewPlatformStatistics,
        Action::kQueryAllLogs}},
  };
  const auto found = matrix.find(role);
  if (found == matrix.end()) {
    return false;
  }
  return found->second.count(action) > 0;
}

std::string ToString(Action action) {
  switch (action) {
    case Action::kBrowseCatalog:
      return "浏览菜品";
    case Action::kPlaceOrder:
      return "提交订单";
    case Action::kCancelOwnOrder:
      return "取消自己的订单";
    case Action::kQueryOwnOrder:
      return "查询自己的订单";
    case Action::kManageShop:
      return "管理店铺";
    case Action::kManageDish:
      return "管理菜品";
    case Action::kAcceptOrReject:
      return "接单或拒单";
    case Action::kFinishCooking:
      return "制作完成";
    case Action::kAssignDelivery:
      return "指派配送";
    case Action::kPickUpAndDeliver:
      return "取餐与送达";
    case Action::kReportDeliveryFailure:
      return "报备配送失败";
    case Action::kAuditQualification:
      return "审核资质";
    case Action::kHandleRefund:
      return "处理退款";
    case Action::kViewShopStatistics:
      return "查看本店统计";
    case Action::kViewPlatformStatistics:
      return "查看平台统计";
    case Action::kQueryAllLogs:
      return "查询全部日志";
  }
  return "未知操作";
}

const std::vector<OrderStatus>& OrderStateMachine::AllStatuses() {
  static const std::vector<OrderStatus> statuses = {
      OrderStatus::kPending,        OrderStatus::kAccepted,
      OrderStatus::kReady,          OrderStatus::kDelivering,
      OrderStatus::kCompleted,      OrderStatus::kCancelled,
      OrderStatus::kRejected,       OrderStatus::kDeliveryFailed,
      OrderStatus::kRefunded,
  };
  return statuses;
}

bool OrderStateMachine::CanTransit(OrderStatus from, OrderStatus to) {
  const auto& table = TransitionTable();
  const auto found = table.find(from);
  if (found == table.end()) {
    return false;
  }
  const auto& targets = found->second;
  return std::find(targets.begin(), targets.end(), to) != targets.end();
}

void OrderStateMachine::EnsureTransition(OrderStatus from, OrderStatus to) {
  Check(CanTransit(from, to), ErrorCode::kStateChanged,
        "订单状态不允许从 " + ToString(from) + " 变更为 " + ToString(to));
}

std::vector<OrderStatus> OrderStateMachine::AllowedTargets(OrderStatus from) {
  const auto& table = TransitionTable();
  const auto found = table.find(from);
  if (found == table.end()) {
    return {};
  }
  return found->second;
}

std::size_t OrderStateMachine::LegalTransitionCount() {
  std::size_t count = 0;
  for (const auto& entry : TransitionTable()) {
    count += entry.second.size();
  }
  return count;
}

std::string HashPassword(const std::string& raw_password) {
  const std::string salted = "delivery::" + raw_password + "::2026";
  return ToHex(Fnv1a(salted));
}

bool VerifyPassword(const std::string& raw_password,
                    const std::string& hash) {
  return HashPassword(raw_password) == hash;
}

bool IsStrongEnoughPassword(const std::string& raw_password) {
  return raw_password.size() >= 8 && raw_password.size() <= 32;
}

bool IsValidPhone(const std::string& phone) {
  if (phone.size() != 11) {
    return false;
  }
  if (phone.front() != '1') {
    return false;
  }
  for (char c : phone) {
    if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
      return false;
    }
  }
  return true;
}

void ValidatePrice(Money price) {
  Check(price > 0, ErrorCode::kInvalidArgument, "菜品价格必须大于 0");
  Check(price <= 100000000, ErrorCode::kInvalidArgument,
        "菜品价格超出上限（100 万元）");
}

void ValidateQuantity(int quantity) {
  Check(quantity >= 1 && quantity <= 99, ErrorCode::kInvalidArgument,
        "购买数量必须在 1 到 99 之间");
}

void ValidateDishName(const std::string& name) {
  const std::string normalized = NormalizeText(name);
  Check(!normalized.empty(), ErrorCode::kInvalidArgument, "菜品名称不能为空");
  Check(normalized.size() <= 60, ErrorCode::kInvalidArgument,
        "菜品名称过长");
}

std::string MaskPhone(const std::string& phone) {
  if (phone.size() != 11) {
    return phone;
  }
  return phone.substr(0, 3) + "****" + phone.substr(7);
}

}  // namespace domain
}  // namespace delivery
