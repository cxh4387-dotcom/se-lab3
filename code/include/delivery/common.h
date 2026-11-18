// Copyright 2026 251220048 Cao Xuhang. All rights reserved.
// Basic types, error handling, clock and id helpers shared by all layers.

#ifndef DELIVERY_COMMON_H_
#define DELIVERY_COMMON_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace delivery {

// Money is always stored as integer cents to avoid floating point errors.
using Money = std::int64_t;

using UserId = std::int64_t;
using ShopId = std::int64_t;
using DishId = std::int64_t;
using OrderId = std::int64_t;
using TaskId = std::int64_t;
using RefundId = std::int64_t;
using LogId = std::int64_t;

using TimePoint = std::chrono::system_clock::time_point;
using Minutes = std::chrono::minutes;

// Business error codes. The CLI maps them to user facing messages and the
// HTTP layer of the production system would map them to status codes.
enum class ErrorCode {
  kInvalidArgument = 400,
  kPermissionDenied = 403,
  kNotFound = 404,
  kConflict = 409,
  kOutOfStock = 4091,
  kPriceChanged = 4092,
  kStateChanged = 4093,
  kDuplicateRequest = 4094,
  kLocked = 423,
};

// All business failures are raised as DomainError so that callers cannot
// silently ignore them the way a returned bool could be ignored.
class DomainError : public std::runtime_error {
 public:
  DomainError(ErrorCode code, const std::string& message)
      : std::runtime_error(message), code_(code) {}

  ErrorCode code() const noexcept { return code_; }

 private:
  ErrorCode code_;
};

// Throws DomainError when the condition is false. Keeps the services short.
inline void Check(bool condition, ErrorCode code, const std::string& message) {
  if (!condition) {
    throw DomainError(code, message);
  }
}

std::string FormatMoney(Money cents);
std::string FormatTime(const TimePoint& point);

// Trims leading and trailing spaces and collapses inner runs of spaces.
std::string NormalizeText(const std::string& text);

bool ContainsIgnoreCase(const std::string& haystack, const std::string& needle);

// Clock is injected so that timeout rules (5 minutes for accepting an order,
// 30 minutes for confirming delivery) can be tested without sleeping.
class Clock {
 public:
  virtual ~Clock() = default;
  virtual TimePoint Now() const = 0;
};

class SystemClock : public Clock {
 public:
  TimePoint Now() const override;
};

class FakeClock : public Clock {
 public:
  explicit FakeClock(TimePoint start);
  TimePoint Now() const override;
  void Advance(Minutes minutes);
  void AdvanceSeconds(int seconds);

 private:
  TimePoint current_;
};

// Monotonic identifiers. A real deployment would use a snowflake generator,
// the sequential version keeps traceability inside one process run. The
// counter is atomic because order submission runs from several threads.
class IdGenerator {
 public:
  explicit IdGenerator(std::int64_t start = 1) : next_(start) {}
  std::int64_t Next() { return next_.fetch_add(1); }
  void Reset(std::int64_t start) { next_.store(start); }

 private:
  std::atomic<std::int64_t> next_;
};

// 12 characters order number, e.g. 20261106000188.
std::string MakeOrderNo(const TimePoint& created_at, std::int64_t sequence);

}  // namespace delivery

#endif  // DELIVERY_COMMON_H_
