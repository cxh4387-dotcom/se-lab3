cmake_minimum_required(VERSION 3.16)
project(delivery LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DELIVERY_BUILD_TESTS "Build unit tests (needs network)" OFF)

find_package(Threads REQUIRED)

add_library(delivery_core
  src/common.cpp
  src/domain_types.cpp
  src/domain_rules.cpp
  src/memory_repository.cpp
  src/auth_service.cpp
  src/catalog_service.cpp
  src/cart_service.cpp
  src/order_service.cpp
  src/delivery_service.cpp
  src/refund_service.cpp
  src/stats_service.cpp
  src/audit_service.cpp
)

target_include_directories(delivery_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_options(delivery_core PRIVATE
  -Wall -Wextra -Wpedantic)

target_link_libraries(delivery_core PUBLIC Threads::Threads)

add_executable(delivery_cli apps/main.cpp)
target_link_libraries(delivery_cli PRIVATE delivery_core)

if (DELIVERY_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_executable(delivery_tests
    tests/test_state_machine.cpp
    tests/test_order_service.cpp
    tests/test_inventory.cpp
    tests/test_refund.cpp
    tests/test_access_control.cpp
  )
  target_include_directories(delivery_tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(delivery_tests
    PRIVATE delivery_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(delivery_tests)
endif ()
