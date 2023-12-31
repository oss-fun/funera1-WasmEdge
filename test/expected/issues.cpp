// SPDX-License-Identifier: CC0-1.0
#include <cstdint>
#include <experimental/expected.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static cxx20::expected<int, std::string> getInt3(int val) { return val; }

static cxx20::expected<int, std::string> getInt2(int val) { return val; }

static cxx20::expected<int, std::string> getInt1() {
  return getInt2(5).and_then(getInt3);
}

TEST(RegressionTest, Issue1) { getInt1(); }

static cxx20::expected<int, int> operation1() { return 42; }

static cxx20::expected<std::string, int> operation2(int const) {
  return "Bananas";
}

TEST(RegressionTest, Issue17) {
  auto const intermediate_result = operation1();

  intermediate_result.and_then(operation2);
}

struct a {};
struct b : a {};

static auto doit() -> cxx20::expected<std::unique_ptr<b>, int> {
  return cxx20::unexpected(0);
}

TEST(RegressionTest, Issue23) {
  cxx20::expected<std::unique_ptr<a>, int> msg = doit();
  EXPECT_FALSE(msg.has_value());
}

TEST(RegressionTest, Issue26) {
  cxx20::expected<a, int> exp = cxx20::expected<b, int>(cxx20::unexpect, 0);
  EXPECT_FALSE(exp.has_value());
}

struct foo {
  foo() = default;
  foo(foo &) = delete;
  foo(foo &&) {}
};

TEST(RegressionTest, Issue29) {
  std::vector<foo> v;
  v.emplace_back();
  cxx20::expected<std::vector<foo>, int> ov = std::move(v);
  EXPECT_EQ(ov->size(), UINT32_C(1));
}

static cxx20::expected<int, std::string> error() {
  return cxx20::unexpected(std::string("error1 "));
}
static std::string maperror(std::string s) { return s + "maperror "; }

TEST(RegressionTest, Issue30) { error().map_error(maperror); }

struct i31 {
  int i;
};
TEST(RegressionTest, Issue31) {
  const cxx20::expected<i31, int> a = i31{42};
  EXPECT_EQ(a->i, 42);

  cxx20::expected<void, std::string> result;
  cxx20::expected<void, std::string> result2 = result;
  result2 = result;
}

TEST(RegressionTest, Issue33) {
  cxx20::expected<void, int> res{cxx20::unexpect, 0};
  EXPECT_FALSE(res);
  res = res.map_error([](int) { return 42; });
  EXPECT_EQ(res.error(), 42);
}

static cxx20::expected<void, std::string> voidWork() { return {}; }
static cxx20::expected<int, std::string> work2() { return 42; }
static void errorhandling(std::string) {}

TEST(RegressionTest, Issue34) {
  cxx20::expected<int, std::string> result = voidWork().and_then(work2);
  result.map_error([&](std::string r) { errorhandling(r); });
}

struct non_copyable {
  non_copyable(non_copyable &&) = default;
  non_copyable(non_copyable const &) = delete;
  non_copyable() = default;
};

TEST(RegressionTest, Issue42) {
  cxx20::expected<non_copyable, int>{}.map([](non_copyable) {});
}

TEST(RegressionTest, Issue43) {
  auto result = cxx20::expected<void, std::string>{};
  result = cxx20::unexpected(std::string{"foo"});
}

using MaybeDataPtr = cxx20::expected<int, std::unique_ptr<int>>;

static MaybeDataPtr test(int i) noexcept { return i; }

static MaybeDataPtr test2(int i) noexcept { return i; }

TEST(RegressionTest, Issue49) { auto m = test(10).and_then(test2); }

static cxx20::expected<int, std::unique_ptr<std::string>> func() { return 1; }

TEST(RegressionTest, Issue61) { EXPECT_EQ(func().value(), 1); }

struct move_tracker {
  int moved = 0;

  move_tracker() = default;
  move_tracker(move_tracker const &) noexcept {}
  move_tracker(move_tracker &&orig) noexcept : moved(orig.moved + 1) {}

  move_tracker &operator=(move_tracker const &) noexcept { return *this; }
  move_tracker &operator=(move_tracker &&orig) noexcept {
    moved = orig.moved + 1;
    return *this;
  }
};

TEST(RegressionTest, Issue122) {
  cxx20::expected<move_tracker, int> res;
  res.emplace();
  EXPECT_EQ(res.value().moved, 0);
}

#ifdef __cpp_deduction_guides
TEST(RegressionTest, Issue89) {
  auto s = cxx20::unexpected("Some string");
  EXPECT_EQ(s.value(), std::string("Some string"));
}
#endif

struct S {
  int i = 0;
  int j = 0;
  S(int i) : i(i) {}
  S(int i, int j) : i(i), j(j) {}
};

TEST(RegressionTest, Issue107) {
  cxx20::expected<int, S> ex1(cxx20::unexpect, 2);
  cxx20::expected<int, S> ex2(cxx20::unexpect, 2, 2);

  EXPECT_EQ(ex1.error().i, 2);
  EXPECT_EQ(ex1.error().j, 0);
  EXPECT_EQ(ex2.error().i, 2);
  EXPECT_EQ(ex2.error().j, 2);
}

TEST(RegressionTest, Issue129) {
  cxx20::expected<std::unique_ptr<int>, int> x1{
      std::unique_ptr<int>(new int(4))};
  cxx20::expected<std::unique_ptr<int>, int> y1{
      std::unique_ptr<int>(new int(2))};
  x1 = std::move(y1);

  EXPECT_EQ(**x1, 2);
}
