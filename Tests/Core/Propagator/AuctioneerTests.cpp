// This file is part of the Acts project.
//
// Copyright (C) 2016-2018 Acts project team
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

///  Boost include(s)
#define BOOST_TEST_MODULE Auctioneer Tests

#include <boost/test/included/unit_test.hpp>
// leave blank line

#include <vector>
#include "Acts/Propagator/detail/Auctioneer.hpp"

namespace Acts {
namespace Test {

  BOOST_AUTO_TEST_CASE(AuctioneerTest_VoidAuctioneer)
  {
    // Build arbitrary vector
    std::vector<bool> vec = {false, true, false, true};
    // Let it run through auction
    detail::VoidAuctioneer va;
    std::vector<bool>      resultVa = va(vec);
    // Test that vector did not change
    BOOST_CHECK_EQUAL_COLLECTIONS(
        vec.begin(), vec.end(), resultVa.begin(), resultVa.end());
  }

  BOOST_AUTO_TEST_CASE(AuctioneerTest_FirstValidAuctioneer)
  {
    // Build arbitrary vector
    std::vector<bool> vec = {false, true, false, true};
    // Let it run through auction
    detail::FirstValidAuctioneer fva;
    std::vector<bool>            resultFva = fva(vec);
    std::vector<bool>            expected  = {false, true, false, false};
    // Test that vector did not change
    BOOST_CHECK_EQUAL_COLLECTIONS(
        expected.begin(), expected.end(), resultFva.begin(), resultFva.end());
  }
}  // namespace Test
}  // namespace Acts