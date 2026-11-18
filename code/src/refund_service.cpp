// Copyright 2026 251220048 Cao Xuhang. All rights reserved.

#include <string>
#include <vector>

#include "delivery/services.h"

namespace delivery {

RefundService::RefundService(const Repositories& repos, Clock* clock,
                             IdGenerator* ids, AuditService* audit)
    : repos_(repos), clock_(clock), ids_(ids), audit_(audit) {}

Refund RefundService::Create(const User& actor, OrderId order_id, Money amount,
                             const std::string& trigger) {
  const auto existing = repos_.refunds.FindByOrderId(order_id);
  if (existing.has_value()) {
    if (existing->status != RefundStatus::kFailed) {
      return existing.value();
    }
    return existing.value();
  }

  Refund refund;
  refund.id = ids_->Next();
  refund.refund_no = "RF" + std::to_string(refund.id);
  refund.order_id = order_id;
  refund.amount = amount;
  refund.status = RefundStatus::kRefunding;
  refund.trigger = trigger;
  refund.created_at = clock_->Now();
  refund.updated_at = refund.created_at;
  repos_.refunds.Save(refund);
  audit_->Record(actor, "登记退款", "refund", refund.refund_no, "",
                 ToString(refund.status), "成功");
  return refund;
}

void RefundService::Retry(const User& admin, RefundId refund_id,
                          bool offline_ok) {
  Check(admin.role == Role::kAdmin, ErrorCode::kPermissionDenied,
        "只有管理员可以处理退款异常");
  const auto found = repos_.refunds.FindById(refund_id);
  Check(found.has_value(), ErrorCode::kNotFound, "退款单不存在");
  Refund refund = found.value();
  Check(refund.status != RefundStatus::kRefunded, ErrorCode::kStateChanged,
        "该退款单已完成，不能重复退款");

  refund.retry_count += 1;
  refund.updated_at = clock_->Now();
  if (offline_ok) {
    refund.status = RefundStatus::kRefunded;
    refund.fail_reason.clear();
    repos_.refunds.Save(refund);
    audit_->Record(admin, "退款重试成功", "refund", refund.refund_no,
                   "退款异常", "已退款", "成功");
    if (orders_ != nullptr) {
      orders_->MarkRefunded(refund.order_id, admin);
    }
  } else {
    refund.status = RefundStatus::kFailed;
    refund.fail_reason = "线下退款未完成";
    repos_.refunds.Save(refund);
    audit_->Record(admin, "退款重试失败", "refund", refund.refund_no,
                   "退款异常", "退款异常", "失败");
  }
}

void RefundService::MarkSucceeded(RefundId refund_id, const User& actor) {
  const auto found = repos_.refunds.FindById(refund_id);
  Check(found.has_value(), ErrorCode::kNotFound, "退款单不存在");
  Refund refund = found.value();
  Check(refund.status != RefundStatus::kRefunded, ErrorCode::kStateChanged,
        "该退款单已完成");
  refund.status = RefundStatus::kRefunded;
  refund.updated_at = clock_->Now();
  repos_.refunds.Save(refund);
  audit_->Record(actor, "退款完成", "refund", refund.refund_no, "退款中",
                 "已退款", "成功");
  if (orders_ != nullptr) {
    orders_->MarkRefunded(refund.order_id, actor);
  }
}

std::optional<Refund> RefundService::FindByOrder(OrderId order_id) const {
  return repos_.refunds.FindByOrderId(order_id);
}

std::vector<Refund> RefundService::ListFailed() const {
  std::vector<Refund> result;
  for (const Refund& refund : repos_.refunds.ListAll()) {
    if (refund.status == RefundStatus::kFailed) {
      result.push_back(refund);
    }
  }
  return result;
}

}  // namespace delivery
