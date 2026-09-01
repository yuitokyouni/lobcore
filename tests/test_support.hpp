#pragma once

#include <lobcore/types.hpp>

namespace lobcore::test_support {

inline Timestamp g_next_timestamp = 1;

inline void reset_timestamps() { g_next_timestamp = 1; }

inline Timestamp next_timestamp() { return g_next_timestamp++; }

}  // namespace lobcore::test_support
