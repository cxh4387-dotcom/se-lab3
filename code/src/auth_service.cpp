// Copyright 2026 251220048 Cao Xuhang. All rights reserved.

#include <string>

#include "delivery/services.h"

namespace delivery {
namespace {

constexpr int kMaxFailedAttempts = 5;
constexpr int kLockMinutes = 10;

}  // namespace

AuthService::AuthService(const Repositories& repos, Clock* clock,
                         IdGenerator* ids, AuditService* audit)
    : repos_(repos), clock_(clock), ids_(ids), audit_(audit) {}

User AuthService::Register(const std::string& phone,
                           const std::string& password, Role role) {
  Check(domain::IsValidPhone(phone), ErrorCode::kInvalidArgument,
        "手机号格式不正确");
  Check(domain::IsStrongEnoughPassword(password), ErrorCode::kInvalidArgument,
        "密码长度需为 8 到 32 位");
  Check(!repos_.users.FindByPhone(phone).has_value(),
        ErrorCode::kDuplicateRequest, "该手机号已注册");

  User user;
  user.id = ids_->Next();
  user.phone = phone;
  user.password_hash = domain::HashPassword(password);
  user.role = role;
  user.audit_status =
      (role == Role::kMerchant || role == Role::kRider)
          ? AuditStatus::kPending
          : AuditStatus::kApproved;
  user.created_at = clock_->Now();
  repos_.users.Save(user);

  audit_->Record(user, "注册", "user", std::to_string(user.id), "", "待审核",
                 "成功");
  return user;
}

User AuthService::Login(const std::string& phone,
                        const std::string& password) {
  const auto found = repos_.users.FindByPhone(phone);
  Check(found.has_value(), ErrorCode::kNotFound, "账号不存在");
  User user = found.value();
  Check(!user.IsLocked(clock_->Now()), ErrorCode::kLocked,
        "账号已锁定，请十分钟后重试");

  if (!domain::VerifyPassword(password, user.password_hash)) {
    user.failed_attempts += 1;
    if (user.failed_attempts >= kMaxFailedAttempts) {
      user.locked_until = clock_->Now() + Minutes(kLockMinutes);
      user.failed_attempts = 0;
    }
    repos_.users.Save(user);
    throw DomainError(ErrorCode::kPermissionDenied, "手机号或密码错误");
  }

  if (user.failed_attempts != 0) {
    user.failed_attempts = 0;
    repos_.users.Save(user);
  }
  audit_->Record(user, "登录", "user", std::to_string(user.id), "", "",
                 "成功");
  return user;
}

void AuthService::SubmitQualification(UserId user_id,
                                      const std::string& license_no,
                                      const std::string& id_card_no,
                                      const std::string& health_no) {
  User user = Require(user_id);
  Check(user.role == Role::kMerchant || user.role == Role::kRider,
        ErrorCode::kPermissionDenied, "只有商家或配送人员需要提交资质");

  if (user.role == Role::kMerchant) {
    Check(!license_no.empty(), ErrorCode::kInvalidArgument,
          "营业执照编号不能为空");
    const auto same_license = repos_.users.FindByLicenseNo(license_no);
    Check(!same_license.has_value() || same_license->id == user.id,
          ErrorCode::kDuplicateRequest, "营业执照编号已被占用");
    user.license_no = license_no;
  } else {
    Check(!id_card_no.empty() && !health_no.empty(),
          ErrorCode::kInvalidArgument, "身份证号与健康证编号不能为空");
    const auto same_id = repos_.users.FindByIdCardNo(id_card_no);
    Check(!same_id.has_value() || same_id->id == user.id,
          ErrorCode::kDuplicateRequest, "身份证号已被占用");
    user.id_card_no = id_card_no;
    user.health_no = health_no;
  }
  user.audit_status = AuditStatus::kPending;
  repos_.users.Save(user);
  audit_->Record(user, "提交资质", "user", std::to_string(user.id), "",
                 "待审核", "成功");
}

void AuthService::ApproveUser(const User& admin, UserId user_id) {
  EnsureCan(admin, domain::Action::kAuditQualification);
  User user = Require(user_id);
  Check(user.role != Role::kCustomer, ErrorCode::kInvalidArgument,
        "顾客账号无需审核");
  const std::string before = ToString(user.audit_status);
  user.audit_status = AuditStatus::kApproved;
  repos_.users.Save(user);
  audit_->Record(admin, "通过资质审核", "user", std::to_string(user_id),
                 before, "已通过", "成功");
}

void AuthService::RejectUser(const User& admin, UserId user_id,
                             const std::string& reason) {
  EnsureCan(admin, domain::Action::kAuditQualification);
  Check(!reason.empty(), ErrorCode::kInvalidArgument, "驳回原因不能为空");
  User user = Require(user_id);
  const std::string before = ToString(user.audit_status);
  user.audit_status = AuditStatus::kRejected;
  repos_.users.Save(user);
  audit_->Record(admin, "驳回资质", "user", std::to_string(user_id), before,
                 "已驳回：" + reason, "成功");
}

User AuthService::Require(UserId user_id) const {
  const auto found = repos_.users.FindById(user_id);
  Check(found.has_value(), ErrorCode::kNotFound, "用户不存在");
  return found.value();
}

void AuthService::EnsureCan(const User& user, domain::Action action) const {
  // The caller may hold a snapshot taken at login time, so the permission
  // check always works on the freshly stored user record.
  User current = user;
  const auto fresh = repos_.users.FindById(user.id);
  if (fresh.has_value()) {
    current = fresh.value();
  }
  if (!domain::RoleCan(current.role, action)) {
    throw DomainError(ErrorCode::kPermissionDenied,
                      ToString(current.role) + "无权执行：" +
                          domain::ToString(action));
  }
  Check(current.audit_status == AuditStatus::kApproved ||
            current.role == Role::kCustomer,
        ErrorCode::kPermissionDenied, "账号尚未通过资质审核");
}

}  // namespace delivery
