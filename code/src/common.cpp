// Copyright 2026 251220048 Cao Xuhang. All rights reserved.

#include "delivery/common.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>

namespace delivery {
namespace {

std::tm ToLocalTm(const TimePoint& point) {
  std::time_t raw = std::chrono::system_clock::to_time_t(point);
  std::tm tm_value{};
#ifdef _WIN32
  localtime_s(&tm_value, &raw);
#else
  localtime_r(&raw, &tm_value);
#endif
  return tm_value;
}

std::string ToLower(const std::string& text) {
  std::string result = text;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

std::string PadLeft(std::int64_t value, std::size_t width) {
  std::string text = std::to_string(value);
  while (text.size() < width) {
    text.insert(text.begin(), '0');
  }
  return text;
}

}  // namespace

std::string FormatMoney(Money cents) {
  const bool negative = cents < 0;
  const Money absolute = negative ? -cents : cents;
  const Money fen = absolute % 100;
  std::string result = std::to_string(absolute / 100) + ".";
  if (fen < 10) {
    result += "0";
  }
  result += std::to_string(fen);
  return negative ? "-" + result : result;
}

std::string FormatTime(const TimePoint& point) {
  const std::tm tm_value = ToLocalTm(point);
  return PadLeft(tm_value.tm_year + 1900, 4) + "-" +
         PadLeft(tm_value.tm_mon + 1, 2) + "-" + PadLeft(tm_value.tm_mday, 2) +
         " " + PadLeft(tm_value.tm_hour, 2) + ":" +
         PadLeft(tm_value.tm_min, 2) + ":" + PadLeft(tm_value.tm_sec, 2);
}

std::string NormalizeText(const std::string& text) {
  std::string result;
  result.reserve(text.size());
  bool pending_space = false;
  for (char c : text) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isspace(uc) != 0) {
      pending_space = !result.empty();
      continue;
    }
    if (pending_space) {
      result.push_back(' ');
      pending_space = false;
    }
    result.push_back(c);
  }
  return result;
}

bool ContainsIgnoreCase(const std::string& haystack,
                        const std::string& needle) {
  if (needle.empty()) {
    return true;
  }
  return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
}

TimePoint SystemClock::Now() const { return std::chrono::system_clock::now(); }

FakeClock::FakeClock(TimePoint start) : current_(start) {}

TimePoint FakeClock::Now() const { return current_; }

void FakeClock::Advance(Minutes minutes) { current_ += minutes; }

void FakeClock::AdvanceSeconds(int seconds) {
  current_ += std::chrono::seconds(seconds);
}

std::string MakeOrderNo(const TimePoint& created_at, std::int64_t sequence) {
  const std::tm tm_value = ToLocalTm(created_at);
  return PadLeft(tm_value.tm_year + 1900, 4) + PadLeft(tm_value.tm_mon + 1, 2) +
         PadLeft(tm_value.tm_mday, 2) + PadLeft(sequence % 100000, 5);
}

}  // namespace delivery
