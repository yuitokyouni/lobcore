#include <catch2/catch_test_macros.hpp>

#include <lobcore/event_log.hpp>

#include <cstddef>
#include <cstring>

using namespace lobcore;

TEST_CASE("all emitted log record kinds have zero padding") {
  LoggedBook logged;
  logged.add_limit(Order{1, Side::Sell, 100, 10, 1}, 1);
  logged.add_limit(Order{2, Side::Buy, 100, 4, 2}, 2);
  logged.add_limit(Order{3, Side::Buy, 100, 0, 3}, 3);
  logged.cancel(1, 4);
  REQUIRE(logged.log().size() == 5);
  for (const auto& record : logged.log()) {
    const auto* raw = reinterpret_cast<const unsigned char*>(&record);
    for (std::size_t offset = 11; offset < 16; ++offset) {
      INFO("kind=" << static_cast<int>(record.kind) << " offset=" << offset);
      CHECK(raw[offset] == 0);
    }
  }
}

TEST_CASE("log hash and replayed market hash ignore padding bytes") {
  LoggedBook logged;
  logged.add_limit(Order{1, Side::Sell, 100, 10, 1}, 1);
  logged.add_limit(Order{2, Side::Buy, 100, 4, 2}, 2);
  auto changed = logged.log();
  for (auto& record : changed) {
    std::memset(reinterpret_cast<unsigned char*>(&record) + 11, 0xA5, 5);
  }
  CHECK(changed == logged.log());
  CHECK(log_hash(changed) == log_hash(logged.log()));
  CHECK(replay(changed).state_hash() == logged.book().state_hash());
}
