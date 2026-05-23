#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#elif __has_include(<catch2/catch.hpp>)
#include <catch2/catch.hpp>
#else
#error "Catch2 headers not found"
#endif

#include "usblink/protocol/SequenceTracker.hpp"

#include <cstdint>
#include <limits>

using usblink::protocol::SequenceStats;
using usblink::protocol::SequenceTracker;

TEST_CASE("SequenceTracker starts uninitialized with zeroed stats", "[protocol][sequence]")
{
    const SequenceTracker tracker;
    const SequenceStats &stats = tracker.stats();
    REQUIRE_FALSE(tracker.initialized());
    REQUIRE(tracker.expectedSequence() == 0U);
    REQUIRE(stats.receivedPackets == 0U);
    REQUIRE(stats.lostPackets == 0U);
    REQUIRE(stats.duplicateOrReorderedPackets == 0U);
}

TEST_CASE("SequenceTracker initializes from the first observed packet", "[protocol][sequence]")
{
    SequenceTracker tracker;
    const uint32_t firstSequence = 41U;
    tracker.observe(firstSequence);
    const SequenceStats &stats = tracker.stats();
    REQUIRE(tracker.initialized());
    REQUIRE(tracker.expectedSequence() == 42U);
    REQUIRE(stats.receivedPackets == 1U);
    REQUIRE(stats.lostPackets == 0U);
    REQUIRE(stats.duplicateOrReorderedPackets == 0U);
}

TEST_CASE("SequenceTracker accepts normal in-order packet flow", "[protocol][sequence]")
{
    SequenceTracker tracker;
    tracker.observe(10U);
    tracker.observe(11U);
    tracker.observe(12U);
    tracker.observe(13U);
    const SequenceStats &stats = tracker.stats();
    REQUIRE(tracker.initialized());
    REQUIRE(tracker.expectedSequence() == 14U);
    REQUIRE(stats.receivedPackets == 4U);
    REQUIRE(stats.lostPackets == 0U);
    REQUIRE(stats.duplicateOrReorderedPackets == 0U);
}

TEST_CASE("SequenceTracker detects packet loss across a sequence gap", "[protocol][sequence]")
{
    SequenceTracker tracker;
    tracker.observe(100U);
    tracker.observe(104U);
    const SequenceStats &stats = tracker.stats();
    REQUIRE(tracker.initialized());
    REQUIRE(tracker.expectedSequence() == 105U);
    REQUIRE(stats.receivedPackets == 2U);
    REQUIRE(stats.lostPackets == 3U);
    REQUIRE(stats.duplicateOrReorderedPackets == 0U);
}

TEST_CASE("SequenceTracker handles a duplicate packet without advancing expected sequence",
          "[protocol][sequence]")
{
    SequenceTracker tracker;
    tracker.observe(7U);
    tracker.observe(7U);
    const SequenceStats &stats = tracker.stats();
    REQUIRE(tracker.initialized());
    REQUIRE(tracker.expectedSequence() == 8U);
    REQUIRE(stats.receivedPackets == 1U);
    REQUIRE(stats.lostPackets == 0U);
    REQUIRE(stats.duplicateOrReorderedPackets == 1U);
}

TEST_CASE("SequenceTracker handles a reordered older packet without advancing expected sequence",
          "[protocol][sequence]")
{
    SequenceTracker tracker;
    tracker.observe(20U);
    tracker.observe(21U);
    tracker.observe(22U);
    tracker.observe(21U);
    const SequenceStats &stats = tracker.stats();
    REQUIRE(tracker.initialized());
    REQUIRE(tracker.expectedSequence() == 23U);
    REQUIRE(stats.receivedPackets == 3U);
    REQUIRE(stats.lostPackets == 0U);
    REQUIRE(stats.duplicateOrReorderedPackets == 1U);
}

TEST_CASE("SequenceTracker accumulates multiple consecutive loss gaps", "[protocol][sequence]")
{
    SequenceTracker tracker;
    tracker.observe(1U);
    tracker.observe(4U);
    tracker.observe(9U);
    const SequenceStats &stats = tracker.stats();
    REQUIRE(tracker.initialized());
    REQUIRE(tracker.expectedSequence() == 10U);
    REQUIRE(stats.receivedPackets == 3U);
    REQUIRE(stats.lostPackets == 6U);
    REQUIRE(stats.duplicateOrReorderedPackets == 0U);
}

TEST_CASE("SequenceTracker counts repeated duplicate packets independently", "[protocol][sequence]")
{
    SequenceTracker tracker;
    tracker.observe(30U);
    tracker.observe(30U);
    tracker.observe(30U);
    tracker.observe(30U);
    const SequenceStats &stats = tracker.stats();
    REQUIRE(tracker.initialized());
    REQUIRE(tracker.expectedSequence() == 31U);
    REQUIRE(stats.receivedPackets == 1U);
    REQUIRE(stats.lostPackets == 0U);
    REQUIRE(stats.duplicateOrReorderedPackets == 3U);
}

TEST_CASE("SequenceTracker preserves expected sequence after duplicates then resumes in-order",
          "[protocol][sequence]")
{
    SequenceTracker tracker;
    tracker.observe(50U);
    tracker.observe(51U);
    tracker.observe(50U);
    tracker.observe(51U);
    tracker.observe(52U);
    const SequenceStats &stats = tracker.stats();
    REQUIRE(tracker.initialized());
    REQUIRE(tracker.expectedSequence() == 53U);
    REQUIRE(stats.receivedPackets == 3U);
    REQUIRE(stats.lostPackets == 0U);
    REQUIRE(stats.duplicateOrReorderedPackets == 2U);
}

TEST_CASE("SequenceTracker accumulates received lost and reordered stats across mixed traffic",
          "[protocol][sequence]")
{
    SequenceTracker tracker;
    tracker.observe(5U);
    tracker.observe(6U);
    tracker.observe(10U);
    tracker.observe(8U);
    tracker.observe(10U);
    tracker.observe(11U);
    tracker.observe(15U);
    tracker.observe(14U);
    const SequenceStats &stats = tracker.stats();
    REQUIRE(tracker.initialized());
    REQUIRE(tracker.expectedSequence() == 16U);
    REQUIRE(stats.receivedPackets == 5U);
    REQUIRE(stats.lostPackets == 6U);
    REQUIRE(stats.duplicateOrReorderedPackets == 3U);
}

TEST_CASE("SequenceTracker wraps expected sequence after observing maximum uint32 sequence",
          "[protocol][sequence]")
{
    SequenceTracker tracker;
    const uint32_t maxSequence = std::numeric_limits<uint32_t>::max();
    tracker.observe(maxSequence);
    const SequenceStats &stats = tracker.stats();
    REQUIRE(tracker.initialized());
    REQUIRE(tracker.expectedSequence() == 0U);
    REQUIRE(stats.receivedPackets == 1U);
    REQUIRE(stats.lostPackets == 0U);
    REQUIRE(stats.duplicateOrReorderedPackets == 0U);
}
