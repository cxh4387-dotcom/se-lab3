// Copyright 2026 251220048 Cao Xuhang. All rights reserved.

#include <string>
#include <vector>

#include "delivery/services.h"

namespace delivery {

DeliveryService::DeliveryService(const Repositories& repos, Clock* clock,
                                 IdGenerator* ids, AuditService* audit,
                                 OrderService* orders)
    : repos_(repos), clock_(clock), ids_(ids), audit_(audit), orders_(orders) {}

DeliveryTask DeliveryService::Assign(const User& actor,
                                     const std::string& order_no,
                                     UserId rider_id) {
  Check(actor.role == Role::kMerchant || actor.role == Role::kAdmin,
        ErrorCode::kPermissionDenied, "只有商家或管理员可以指派配送");
  const auto order = repos_.orders.FindByNo(order_no);
  Check(order.has_value(), ErrorCode::kNotFound, "订单不存在");
  if (actor.role == Role::kMerchant) {
    const auto shop = repos_.shops.FindById(order->shop_id);
    Check(shop.has_value() && shop->owner_id == actor.id,
          ErrorCode::kPermissionDenied, "只能处理本店订单");
  }
  Check(order->status == OrderStatus::kReady, ErrorCode::kStateChanged,
        "只有待取餐的订单才能指派配送");
  Check(!repos_.deliveries.FindByOrderId(order->id).has_value(),
        ErrorCode::kDuplicateRequest, "该订单已有配送单");

  const auto rider = repos_.users.FindById(rider_id);
  Check(rider.has_value(), ErrorCode::kNotFound, "配送人员不存在");
  Check(rider->role == Role::kRider, ErrorCode::kInvalidArgument,
        "该账号不是配送人员");
  Check(rider->audit_status == AuditStatus::kApproved,
        ErrorCode::kPermissionDenied, "配送人员尚未通过资质审核");

  DeliveryTask task;
  task.id = ids_->Next();
  task.order_id = order->id;
  task.rider_id = rider_id;
  task.status = DeliveryStatus::kWaiting;
  task.assigned_at = clock_->Now();
  repos_.deliveries.Save(task);
  audit_->Record(actor, "指派配送", "order", order_no, "",
                 "配送单 " + std::to_string(task.id), "成功");
  return task;
}

void DeliveryService::AcceptTask(const User& rider, TaskId task_id) {
  DeliveryTask task = LoadTask(task_id);
  EnsureRiderOwns(rider, task);
  Check(task.status == DeliveryStatus::kWaiting, ErrorCode::kStateChanged,
        "该配送单已被接走");
  task.status = DeliveryStatus::kAccepted;
  task.accepted_at = clock_->Now();
  repos_.deliveries.Save(task);

  const auto order = repos_.orders.FindById(task.order_id);
  Check(order.has_value(), ErrorCode::kNotFound, "订单不存在");
  if (order->status == OrderStatus::kReady) {
    Order updated = order.value();
    domain::OrderStateMachine::EnsureTransition(updated.status,
                                                OrderStatus::kDelivering);
    updated.status = OrderStatus::kDelivering;
    updated.version += 1;
    updated.picked_at = clock_->Now();
    repos_.orders.Save(updated);
  }
  audit_->Record(rider, "骑手接单", "order", order->order_no, "待取餐",
                 "配送中", "成功");
}

void DeliveryService::PickUp(const User& rider, TaskId task_id) {
  DeliveryTask task = LoadTask(task_id);
  EnsureRiderOwns(rider, task);
  Check(task.status == DeliveryStatus::kAccepted, ErrorCode::kStateChanged,
        "请先接单再取餐");
  task.status = DeliveryStatus::kPicked;
  repos_.deliveries.Save(task);
  audit_->Record(rider, "确认取餐", "delivery", std::to_string(task_id), "",
                 "已取餐", "成功");
}

void DeliveryService::Deliver(const User& rider, TaskId task_id) {
  DeliveryTask task = LoadTask(task_id);
  EnsureRiderOwns(rider, task);
  Check(task.status == DeliveryStatus::kPicked, ErrorCode::kStateChanged,
        "未取餐不能直接送达");
  task.status = DeliveryStatus::kDelivered;
  task.delivered_at = clock_->Now();
  repos_.deliveries.Save(task);

  const auto order = repos_.orders.FindById(task.order_id);
  Check(order.has_value(), ErrorCode::kNotFound, "订单不存在");
  Order updated = order.value();
  updated.delivered_at = clock_->Now();
  updated.version += 1;
  repos_.orders.Save(updated);
  audit_->Record(rider, "确认送达", "order", order->order_no, "配送中",
                 "已送达（待顾客确认）", "成功");
}

void DeliveryService::ReportFailure(const User& rider, TaskId task_id,
                                    const std::string& reason) {
  DeliveryTask task = LoadTask(task_id);
  EnsureRiderOwns(rider, task);
  Check(!NormalizeText(reason).empty(), ErrorCode::kInvalidArgument,
        "报备配送失败必须填写原因");
  Check(task.status == DeliveryStatus::kAccepted ||
            task.status == DeliveryStatus::kPicked,
        ErrorCode::kStateChanged, "当前状态不能报备配送失败");
  task.status = DeliveryStatus::kFailed;
  task.fail_reason = NormalizeText(reason);
  task.fail_count += 1;
  repos_.deliveries.Save(task);

  const auto order = repos_.orders.FindById(task.order_id);
  Check(order.has_value(), ErrorCode::kNotFound, "订单不存在");
  orders_->MarkDeliveryFailed(order->id, task.fail_reason, rider);
}

int DeliveryService::AutoConfirmDelivered(int after_minutes) {
  int confirmed = 0;
  const User system_user = [] {
    User user;
    user.id = 0;
    user.role = Role::kAdmin;
    user.audit_status = AuditStatus::kApproved;
    return user;
  }();
  for (const DeliveryTask& task : repos_.deliveries.ListAll()) {
    if (task.status != DeliveryStatus::kDelivered) {
      continue;
    }
    if (clock_->Now() - task.delivered_at < Minutes(after_minutes)) {
      continue;
    }
    const auto order = repos_.orders.FindById(task.order_id);
    if (!order.has_value() ||
        order->status != OrderStatus::kDelivering) {
      continue;
    }
    Order updated = order.value();
    domain::OrderStateMachine::EnsureTransition(updated.status,
                                                OrderStatus::kCompleted);
    updated.status = OrderStatus::kCompleted;
    updated.version += 1;
    updated.completed_at = clock_->Now();
    repos_.orders.Save(updated);
    audit_->Record(system_user, "超时自动确认", "order", updated.order_no,
                   "配送中", "已完成", "成功");
    ++confirmed;
  }
  return confirmed;
}

DeliveryTask DeliveryService::Reassign(const User& admin, TaskId task_id,
                                       UserId rider_id) {
  Check(admin.role == Role::kAdmin, ErrorCode::kPermissionDenied,
        "只有管理员可以重新派单");
  DeliveryTask task = LoadTask(task_id);
  Check(task.status == DeliveryStatus::kFailed, ErrorCode::kStateChanged,
        "只有配送失败的配送单可以重新派单");
  Check(task.fail_count < 3, ErrorCode::kStateChanged,
        "同一订单配送失败已达 3 次，只能走退款");
  const auto rider = repos_.users.FindById(rider_id);
  Check(rider.has_value() && rider->role == Role::kRider,
        ErrorCode::kInvalidArgument, "配送人员不存在");
  Check(rider->audit_status == AuditStatus::kApproved,
        ErrorCode::kPermissionDenied, "配送人员尚未通过资质审核");

  const auto order = repos_.orders.FindById(task.order_id);
  Check(order.has_value(), ErrorCode::kNotFound, "订单不存在");
  Order updated = order.value();
  domain::OrderStateMachine::EnsureTransition(updated.status,
                                              OrderStatus::kDelivering);
  updated.status = OrderStatus::kDelivering;
  updated.version += 1;
  repos_.orders.Save(updated);

  task.rider_id = rider_id;
  task.status = DeliveryStatus::kWaiting;
  task.assigned_at = clock_->Now();
  repos_.deliveries.Save(task);
  audit_->Record(admin, "重新派单", "delivery", std::to_string(task_id),
                 "配送失败", "待接单", "成功");
  return task;
}

std::vector<DeliveryTask> DeliveryService::ListByRider(
    const User& rider) const {
  Check(rider.role == Role::kRider, ErrorCode::kPermissionDenied,
        "只有配送人员可以查看配送任务");
  return repos_.deliveries.ListByRider(rider.id);
}

DeliveryTask DeliveryService::LoadTask(TaskId task_id) const {
  const auto found = repos_.deliveries.FindById(task_id);
  Check(found.has_value(), ErrorCode::kNotFound, "配送单不存在");
  return found.value();
}

void DeliveryService::EnsureRiderOwns(const User& rider,
                                      const DeliveryTask& task) const {
  Check(rider.role == Role::kRider, ErrorCode::kPermissionDenied,
        "只有配送人员可以操作配送单");
  Check(task.rider_id == rider.id, ErrorCode::kPermissionDenied,
        "只能操作派给自己的配送单");
}

}  // namespace delivery
