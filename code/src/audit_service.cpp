// Copyright 2026 251220048 Cao Xuhang. All rights reserved.

#include <string>
#include <vector>

#include "delivery/services.h"

namespace delivery {

AuditService::AuditService(const Repositories& repos, Clock* clock,
                           IdGenerator* ids)
    : repos_(repos), clock_(clock), ids_(ids) {}

void AuditService::Record(const User& actor, const std::string& action,
                          const std::string& object_type,
                          const std::string& object_id,
                          const std::string& before_state,
                          const std::string& after_state,
                          const std::string& result,
                          const std::string& source_ip) {
  LogEntry entry;
  entry.id = ids_->Next();
  entry.actor_id = actor.id;
  entry.actor_role = actor.role;
  entry.action = action;
  entry.object_type = object_type;
  entry.object_id = object_id;
  entry.before_state = before_state;
  entry.after_state = after_state;
  entry.result = result;
  entry.source_ip = source_ip;
  entry.created_at = clock_->Now();
  repos_.logs.Append(entry);
}

std::vector<LogEntry> AuditService::Query(
    const User& actor, const std::optional<std::string>& action,
    const std::optional<std::string>& object_id,
    const std::optional<TimePoint>& from,
    const std::optional<TimePoint>& to) const {
  const bool sees_everything = actor.role == Role::kAdmin;
  std::vector<LogEntry> result;
  for (const LogEntry& entry : repos_.logs.ListAll()) {
    if (!sees_everything && entry.actor_id != actor.id) {
      continue;
    }
    if (action.has_value() && entry.action != action.value()) {
      continue;
    }
    if (object_id.has_value() && entry.object_id != object_id.value()) {
      continue;
    }
    if (from.has_value() && entry.created_at < from.value()) {
      continue;
    }
    if (to.has_value() && entry.created_at > to.value()) {
      continue;
    }
    result.push_back(entry);
  }
  return result;
}

std::size_t AuditService::Count() const {
  return repos_.logs.ListAll().size();
}

}  // namespace delivery
