// Copyright 2026 251220048 Cao Xuhang. All rights reserved.
// Shared fixture: wires the real services onto the in-memory repositories and
// a controllable clock, so every test exercises production code paths.

#ifndef TESTS_TEST_FIXTURE_H_
#define TESTS_TEST_FIXTURE_H_

#include <ctime>
#include <string>

#include "delivery/services.h"

namespace delivery {
namespace test {

inline TimePoint MakeTime(int year, int month, int day, int hour, int minute) {
  std::tm tm_value{};
  tm_value.tm_year = year - 1900;
  tm_value.tm_mon = month - 1;
  tm_value.tm_mday = day;
  tm_value.tm_hour = hour;
  tm_value.tm_min = minute;
  tm_value.tm_sec = 0;
  tm_value.tm_isdst = -1;
  return std::chrono::system_clock::from_time_t(std::mktime(&tm_value));
}

struct TestContext {
  TestContext() : clock(MakeTime(2026, 11, 18, 12, 0)), repos(store.Bundle()) {
    refunds.AttachOrders(&orders);
  }

  FakeClock clock;
  MemoryRepositories store;
  Repositories repos;
  IdGenerator ids{1000};
  AuditService audit{repos, &clock, &ids};
  AuthService auth{repos, &clock, &ids, &audit};
  CatalogService catalog{repos, &clock, &ids, &audit};
  CartService cart{repos, &clock};
  RefundService refunds{repos, &clock, &ids, &audit};
  OrderService orders{repos, &clock, &ids, &audit, &refunds};
  DeliveryService delivery{repos, &clock, &ids, &audit, &orders};
  StatsService stats{repos, &clock};

  struct World {
    User customer;
    User merchant;
    User rider;
    User admin;
    Shop shop;
    Dish dish;
    Dish cheap_dish;
  };

  // Creates one approved account of each role plus an open shop with two
  // dishes (stock 10 and 3) and returns everything the tests need.
  World Seed() {
    World world;
    world.customer = auth.Register("13800000001", "customer123",
                                   Role::kCustomer);
    world.merchant = auth.Register("13800000002", "merchant123",
                                   Role::kMerchant);
    world.rider = auth.Register("13800000003", "rider12345", Role::kRider);
    world.admin = auth.Register("13800000004", "admin12345", Role::kAdmin);

    auth.SubmitQualification(world.merchant.id, "91330106MA2X8Q", "", "");
    auth.ApproveUser(world.admin, world.merchant.id);
    auth.SubmitQualification(world.rider.id, "", "330106199001011234",
                             "JK-2026-7712");
    auth.ApproveUser(world.admin, world.rider.id);
    world.merchant = auth.Require(world.merchant.id);
    world.rider = auth.Require(world.rider.id);

    world.shop = catalog.CreateShop(world.merchant, "测试店铺", "教学区 1 号",
                                    "91330106MA2X8Q", 0, 24 * 60 - 1, false);
    catalog.ChangeBizStatus(world.merchant, world.shop.id,
                            ShopBizStatus::kOpen, "");
    world.dish = catalog.AddDish(world.merchant, world.shop.id, "招牌米线",
                                 "面食", 1600, "大份", 10);
    world.cheap_dish = catalog.AddDish(world.merchant, world.shop.id,
                                       "小份米线", "面食", 900, "小份", 3);
    return world;
  }

  SubmitOrderRequest Request(const std::string& key) const {
    SubmitOrderRequest request;
    request.address = "宿舍 6 号楼 512";
    request.phone = "13800000001";
    request.idempotency_key = key;
    return request;
  }
};

}  // namespace test
}  // namespace delivery

#endif  // TESTS_TEST_FIXTURE_H_
