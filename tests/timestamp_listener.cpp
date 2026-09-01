#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "test_support.hpp"

struct TimestampResetListener : Catch::EventListenerBase {
  using Catch::EventListenerBase::EventListenerBase;
  void testCaseStarting(Catch::TestCaseInfo const&) override {
    lobcore::test_support::reset_timestamps();
  }
};

CATCH_REGISTER_LISTENER(TimestampResetListener)
