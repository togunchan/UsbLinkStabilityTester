#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#elif __has_include(<catch2/catch.hpp>)
#include <catch2/catch.hpp>
#else
#error "Catch2 headers not found"
#endif

#include "usblink/runtime/RuntimeSession.hpp"

#include <cstdint>

TEST_CASE("RuntimeSession processes the default packet flow successfully", "[runtime][session]")
{
    constexpr uint64_t expectedPacketCount = 100U;
    constexpr uint64_t expectedPayloadSize = 64U;

    usblink::RuntimeSession session;

    REQUIRE_NOTHROW(session.start());

    const usblink::protocol::ProtocolStats &protocolStats = session.protocolStats();
    const usblink::protocol::SequenceStats &sequenceStats = session.sequenceStats();

    REQUIRE_FALSE(session.isRunning());
    REQUIRE(protocolStats.parsedPackets == expectedPacketCount);
    REQUIRE(protocolStats.validPackets == expectedPacketCount);
    REQUIRE(protocolStats.totalPayloadBytes == expectedPacketCount * expectedPayloadSize);
    REQUIRE(sequenceStats.receivedPackets == expectedPacketCount);
    REQUIRE(sequenceStats.lostPackets == 0U);
    REQUIRE(sequenceStats.duplicateOrReorderedPackets == 0U);
}
