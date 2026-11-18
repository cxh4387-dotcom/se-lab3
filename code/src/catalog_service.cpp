// Copyright 2026 251220048 Cao Xuhang. All rights reserved.

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "delivery/services.h"

namespace delivery {
namespace {

bool MatchesKeyword(const Dish& dish, const std::string& shop_name,
                    const std::string& keyword) {
  if (keyword.empty()) {
    return true;
  }
  return ContainsIgnoreCase(dish.name, keyword) ||
         ContainsIgnoreCase(shop_name, keyword);
}

}  // namespace

CatalogService::CatalogService(const Repositories& repos, Clock* clock,
                               IdGenerator* ids, AuditService* audit)
    : repos_(repos), clock_(clock), ids_(ids), audit_(audit) {}

Shop CatalogService::CreateShop(const User& merchant, const std::string& name,
                                const std::string& address,
                                const std::string& license_no, int open_minute,
                                int close_minute, bool close_next_day) {
  EnsureCan(merchant, domain::Action::kManageShop);
  Check(!NormalizeText(name).empty(), ErrorCode::kInvalidArgument,
        "店铺名称不能为空");
  Check(!NormalizeText(address).empty(), ErrorCode::kInvalidArgument,
        "店铺地址不能为空");
  Check(!license_no.empty(), ErrorCode::kInvalidArgument,
        "营业执照编号不能为空");
  Check(!repos_.shops.FindByLicenseNo(license_no).has_value(),
        ErrorCode::kDuplicateRequest, "该营业执照编号已开店");
  Check(open_minute >= 0 && open_minute < 24 * 60, ErrorCode::kInvalidArgument,
        "开店时间不合法");
  Check(close_minute >= 0 && close_minute < 24 * 60,
        ErrorCode::kInvalidArgument, "打烊时间不合法");

  Shop shop;
  shop.id = ids_->Next();
  shop.owner_id = merchant.id;
  shop.name = NormalizeText(name);
  shop.address = NormalizeText(address);
  shop.license_no = license_no;
  shop.open_minute = open_minute;
  shop.close_minute = close_minute;
  shop.close_next_day = close_next_day;
  shop.biz_status = ShopBizStatus::kClosed;
  shop.updated_at = clock_->Now();
  repos_.shops.Save(shop);
  audit_->Record(merchant, "创建店铺", "shop", std::to_string(shop.id), "",
                 ToString(shop.biz_status), "成功");
  return shop;
}

void CatalogService::ChangeBizStatus(const User& merchant, ShopId shop_id,
                                     ShopBizStatus status,
                                     const std::string& reason) {
  EnsureCan(merchant, domain::Action::kManageShop);
  EnsureShopOwner(merchant, shop_id);
  Check(status != ShopBizStatus::kForceClosed, ErrorCode::kPermissionDenied,
        "强制停业只能由平台管理员执行");
  Shop shop = GetShop(shop_id);
  const std::string before = ToString(shop.biz_status);
  if (status == ShopBizStatus::kTempClosed) {
    Check(!NormalizeText(reason).empty(), ErrorCode::kInvalidArgument,
          "临时停业必须填写原因");
    shop.stop_reason = NormalizeText(reason);
  } else {
    shop.stop_reason.clear();
  }
  shop.biz_status = status;
  shop.updated_at = clock_->Now();
  repos_.shops.Save(shop);
  audit_->Record(merchant, "变更营业状态", "shop", std::to_string(shop_id),
                 before, ToString(status), "成功");
}

void CatalogService::ForceClose(const User& admin, ShopId shop_id,
                                const std::string& reason) {
  EnsureCan(admin, domain::Action::kAuditQualification);
  Check(!NormalizeText(reason).empty(), ErrorCode::kInvalidArgument,
        "强制停业必须填写原因");
  Shop shop = GetShop(shop_id);
  const std::string before = ToString(shop.biz_status);
  shop.biz_status = ShopBizStatus::kForceClosed;
  shop.stop_reason = NormalizeText(reason);
  shop.updated_at = clock_->Now();
  repos_.shops.Save(shop);
  audit_->Record(admin, "强制停业", "shop", std::to_string(shop_id), before,
                 "强制停业", "成功");
}

void CatalogService::RefreshBizStatus(const Shop& shop) {
  if (shop.biz_status == ShopBizStatus::kTempClosed ||
      shop.biz_status == ShopBizStatus::kForceClosed) {
    return;
  }
  Shop updated = shop;
  const ShopBizStatus target =
      shop.WithinOpeningHours(ToMinuteOfDay(clock_->Now()))
          ? ShopBizStatus::kOpen
          : ShopBizStatus::kClosed;
  if (target != shop.biz_status) {
    updated.biz_status = target;
    updated.updated_at = clock_->Now();
    repos_.shops.Save(updated);
  }
}

Dish CatalogService::AddDish(const User& merchant, ShopId shop_id,
                             const std::string& name,
                             const std::string& category, Money price,
                             const std::string& spec, int daily_qty) {
  EnsureCan(merchant, domain::Action::kManageDish);
  EnsureShopOwner(merchant, shop_id);
  domain::ValidateDishName(name);
  domain::ValidatePrice(price);
  Check(daily_qty >= 0 && daily_qty <= 9999, ErrorCode::kInvalidArgument,
        "每日库存必须在 0 到 9999 之间");
  const std::string normalized_name = NormalizeText(name);
  const std::string normalized_spec = NormalizeText(spec);
  for (const Dish& existing : repos_.dishes.ListByShop(shop_id)) {
    Check(!(existing.name == normalized_name &&
            existing.spec == normalized_spec),
          ErrorCode::kDuplicateRequest, "同一店铺已存在同名同规格菜品");
  }

  Dish dish;
  dish.id = ids_->Next();
  dish.shop_id = shop_id;
  dish.name = normalized_name;
  dish.category = NormalizeText(category);
  dish.price = price;
  dish.spec = normalized_spec;
  dish.status = DishStatus::kOnSale;
  dish.created_at = clock_->Now();
  dish.price_updated_at = dish.created_at;
  dish.previous_price = price;
  repos_.dishes.Save(dish);

  DailyStock stock;
  stock.dish_id = dish.id;
  stock.biz_date = ToDateKey(clock_->Now());
  stock.total_qty = daily_qty;
  stock.sold_qty = 0;
  repos_.stocks.Save(stock);

  audit_->Record(merchant, "新增菜品", "dish", std::to_string(dish.id), "",
                 FormatMoney(price), "成功");
  return dish;
}

void CatalogService::UpdateDish(const User& merchant, DishId dish_id,
                                const std::string& name,
                                const std::string& category,
                                const std::string& spec) {
  EnsureCan(merchant, domain::Action::kManageDish);
  Dish dish = GetDish(dish_id);
  EnsureShopOwner(merchant, dish.shop_id);
  domain::ValidateDishName(name);
  dish.name = NormalizeText(name);
  dish.category = NormalizeText(category);
  dish.spec = NormalizeText(spec);
  repos_.dishes.Save(dish);
  audit_->Record(merchant, "修改菜品", "dish", std::to_string(dish_id), "", "",
                 "成功");
}

void CatalogService::ChangePrice(const User& merchant, DishId dish_id,
                                 Money new_price) {
  EnsureCan(merchant, domain::Action::kManageDish);
  Dish dish = GetDish(dish_id);
  EnsureShopOwner(merchant, dish.shop_id);
  domain::ValidatePrice(new_price);
  Check(new_price != dish.price, ErrorCode::kInvalidArgument,
        "新价格与原价格相同");
  const std::string before = FormatMoney(dish.price);
  dish.previous_price = dish.price;
  dish.price = new_price;
  dish.price_updated_at = clock_->Now();
  repos_.dishes.Save(dish);
  audit_->Record(merchant, "修改菜品价格", "dish", std::to_string(dish_id),
                 before, FormatMoney(new_price), "成功");
}

void CatalogService::ChangeDishStatus(const User& merchant, DishId dish_id,
                                      DishStatus status) {
  EnsureCan(merchant, domain::Action::kManageDish);
  Dish dish = GetDish(dish_id);
  EnsureShopOwner(merchant, dish.shop_id);
  const std::string before = ToString(dish.status);
  dish.status = status;
  repos_.dishes.Save(dish);
  audit_->Record(merchant, "变更菜品状态", "dish", std::to_string(dish_id),
                 before, ToString(status), "成功");
}

void CatalogService::ReplenishStock(const User& merchant, DishId dish_id,
                                    int extra_qty) {
  EnsureCan(merchant, domain::Action::kManageDish);
  Dish dish = GetDish(dish_id);
  EnsureShopOwner(merchant, dish.shop_id);
  Check(extra_qty > 0 && extra_qty <= 9999, ErrorCode::kInvalidArgument,
        "补货数量必须在 1 到 9999 之间");
  const int biz_date = ToDateKey(clock_->Now());
  DailyStock stock;
  const auto found = repos_.stocks.Find(dish_id, biz_date);
  if (found.has_value()) {
    stock = found.value();
  } else {
    stock.dish_id = dish_id;
    stock.biz_date = biz_date;
  }
  stock.total_qty += extra_qty;
  repos_.stocks.Save(stock);
  if (dish.status == DishStatus::kSoldOut) {
    dish.status = DishStatus::kOnSale;
    repos_.dishes.Save(dish);
  }
  audit_->Record(merchant, "补货", "dish", std::to_string(dish_id),
                 std::to_string(stock.total_qty - extra_qty),
                 std::to_string(stock.total_qty), "成功");
}

void CatalogService::ResetDailyStock(const User& merchant, DishId dish_id,
                                     int total_qty) {
  EnsureCan(merchant, domain::Action::kManageDish);
  Dish dish = GetDish(dish_id);
  EnsureShopOwner(merchant, dish.shop_id);
  Check(total_qty >= 0 && total_qty <= 9999, ErrorCode::kInvalidArgument,
        "每日库存必须在 0 到 9999 之间");
  DailyStock stock;
  stock.dish_id = dish_id;
  stock.biz_date = ToDateKey(clock_->Now());
  stock.total_qty = total_qty;
  stock.sold_qty = 0;
  repos_.stocks.Save(stock);
  audit_->Record(merchant, "重置每日库存", "dish", std::to_string(dish_id), "",
                 std::to_string(total_qty), "成功");
}

PageResult CatalogService::Search(const DishQuery& query) const {
  PageResult result;
  result.page = std::max(1, query.page);
  result.page_size = std::max(1, query.page_size);

  std::map<ShopId, Shop> shops;
  for (const Shop& shop : repos_.shops.ListAll()) {
    shops[shop.id] = shop;
  }

  // Sales volume is derived from the orders so that the prototype does not
  // need a materialised counter; production uses a daily statistics table.
  std::map<DishId, int> sales;
  for (const Order& order : repos_.orders.ListAll()) {
    for (const OrderItem& item : order.items) {
      sales[item.dish_id] += item.quantity;
    }
  }

  std::vector<DishView> matched;
  for (const Dish& dish : repos_.dishes.ListAll()) {
    const auto shop_found = shops.find(dish.shop_id);
    if (shop_found == shops.end()) {
      continue;
    }
    const Shop& shop = shop_found->second;
    if (query.only_open_shop && !shop.AcceptsNewOrder(clock_->Now())) {
      continue;
    }
    if (dish.status == DishStatus::kOffShelf) {
      continue;
    }
    if (!query.category.empty() &&
        !ContainsIgnoreCase(dish.category, query.category)) {
      continue;
    }
    if (!MatchesKeyword(dish, shop.name, query.keyword)) {
      continue;
    }
    if (query.min_price >= 0 && dish.price < query.min_price) {
      continue;
    }
    if (query.max_price >= 0 && dish.price > query.max_price) {
      continue;
    }
    DishView view;
    view.dish = dish;
    view.shop_name = shop.name;
    view.remaining = RemainingOf(dish.id);
    view.purchasable = dish.status == DishStatus::kOnSale && view.remaining > 0;
    matched.push_back(view);
  }

  switch (query.sort) {
    case DishQuery::Sort::kPriceAsc:
      std::sort(matched.begin(), matched.end(),
                [](const DishView& a, const DishView& b) {
                  return a.dish.price < b.dish.price;
                });
      break;
    case DishQuery::Sort::kPriceDesc:
      std::sort(matched.begin(), matched.end(),
                [](const DishView& a, const DishView& b) {
                  return a.dish.price > b.dish.price;
                });
      break;
    case DishQuery::Sort::kSales:
    default:
      std::sort(matched.begin(), matched.end(),
                [&sales](const DishView& a, const DishView& b) {
                  const int left = sales.count(a.dish.id) > 0
                                       ? sales.at(a.dish.id)
                                       : 0;
                  const int right = sales.count(b.dish.id) > 0
                                        ? sales.at(b.dish.id)
                                        : 0;
                  return left > right;
                });
      break;
  }

  result.total = static_cast<int>(matched.size());
  const int begin = (result.page - 1) * result.page_size;
  for (int i = begin; i < result.total && i < begin + result.page_size; ++i) {
    if (i >= 0) {
      result.items.push_back(matched[static_cast<std::size_t>(i)]);
    }
  }
  return result;
}

std::vector<DishView> CatalogService::ListShopDishes(ShopId shop_id) const {
  std::vector<DishView> result;
  const auto shop = repos_.shops.FindById(shop_id);
  for (const Dish& dish : repos_.dishes.ListByShop(shop_id)) {
    DishView view;
    view.dish = dish;
    view.shop_name = shop.has_value() ? shop->name : "";
    view.remaining = RemainingOf(dish.id);
    view.purchasable = dish.status == DishStatus::kOnSale && view.remaining > 0;
    result.push_back(view);
  }
  return result;
}

Dish CatalogService::GetDish(DishId dish_id) const {
  const auto found = repos_.dishes.FindById(dish_id);
  Check(found.has_value(), ErrorCode::kNotFound, "菜品不存在");
  return found.value();
}

Shop CatalogService::GetShop(ShopId shop_id) const {
  const auto found = repos_.shops.FindById(shop_id);
  Check(found.has_value(), ErrorCode::kNotFound, "店铺不存在");
  return found.value();
}

DailyStock CatalogService::GetStock(DishId dish_id) const {
  const auto found = repos_.stocks.Find(dish_id, ToDateKey(clock_->Now()));
  Check(found.has_value(), ErrorCode::kNotFound, "该菜品今日未初始化库存");
  return found.value();
}

bool CatalogService::ShopAcceptsOrder(ShopId shop_id) const {
  const auto shop = repos_.shops.FindById(shop_id);
  if (!shop.has_value()) {
    return false;
  }
  return shop->AcceptsNewOrder(clock_->Now());
}

void CatalogService::EnsureShopOwner(const User& merchant,
                                     ShopId shop_id) const {
  const Shop shop = GetShop(shop_id);
  if (merchant.role == Role::kAdmin) {
    return;
  }
  Check(shop.owner_id == merchant.id, ErrorCode::kPermissionDenied,
        "只能操作自己的店铺");
}

int CatalogService::RemainingOf(DishId dish_id) const {
  const auto found = repos_.stocks.Find(dish_id, ToDateKey(clock_->Now()));
  return found.has_value() ? found->Remaining() : 0;
}

void CatalogService::EnsureCan(const User& user,
                               domain::Action action) const {
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
  if (current.role != Role::kCustomer &&
      current.audit_status != AuditStatus::kApproved) {
    throw DomainError(ErrorCode::kPermissionDenied, "账号尚未通过资质审核");
  }
}

}  // namespace delivery
