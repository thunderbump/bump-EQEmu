#pragma once
#include "common/player_zone_access.h"
#include "cppunit/cpptest.h"

class PlayerZoneAccessTest : public Test::Suite {
public:
    PlayerZoneAccessTest() { TEST_ADD(PlayerZoneAccessTest::AccessBoundaries); }
private:
    void AccessBoundaries() {
        using Failure = PlayerZoneAccessFailure;
        TEST_ASSERT(CheckPlayerZoneAccess(2, 0, false, 2, 10, 0, false, false) == Failure::None);
        TEST_ASSERT(CheckPlayerZoneAccess(10, 0, false, 2, 10, 0, false, false) == Failure::None);
        TEST_ASSERT(CheckPlayerZoneAccess(1, 0, false, 2, 10, 0, false, false) == Failure::MinimumLevel);
        TEST_ASSERT(CheckPlayerZoneAccess(11, 0, false, 2, 10, 0, false, false) == Failure::MaximumLevel);
        TEST_ASSERT(CheckPlayerZoneAccess(2, 0, false, 2, 10, 1, false, false) == Failure::Status);
        TEST_ASSERT(CheckPlayerZoneAccess(2, 0, false, 2, 10, 0, true, false) == Failure::Flag);
        TEST_ASSERT(CheckPlayerZoneAccess(2, 0, false, 2, 10, 0, true, true) == Failure::None);
        TEST_ASSERT(CheckPlayerZoneAccess(1, 0, true, 2, 10, 0, true, false) == Failure::None);
        TEST_ASSERT(CheckPlayerZoneAccess(1, 0, true, 2, 10, 1, true, false) == Failure::Status);
    }
};
