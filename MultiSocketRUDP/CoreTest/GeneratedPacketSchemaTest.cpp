#include "PreCompile.h"
#include <gtest/gtest.h>
#include "GeneratedPacketSchema/Protocol.h"
#include "GeneratedPacketSchema/PacketIdType.h"

namespace
{
    GeneratedUser MakeUser()
    {
        GeneratedUser user{};
        user.userId = 12345;
        user.nickname = "nested user";
        user.position = {1.25f, -2.5f};
        user.items = {4, 8, 15};
        user.achievements = {3, 1, 2};
        user.labels = {"alpha", "beta"};
        return user;
    }

    void ExpectUser(const GeneratedUser& actual, const GeneratedUser& expected)
    {
        EXPECT_EQ(actual.userId, expected.userId);
        EXPECT_EQ(actual.nickname, expected.nickname);
        EXPECT_FLOAT_EQ(actual.position.x, expected.position.x);
        EXPECT_FLOAT_EQ(actual.position.y, expected.position.y);
        EXPECT_EQ(actual.items, expected.items);
        EXPECT_EQ(actual.achievements, expected.achievements);
        EXPECT_EQ(actual.labels, expected.labels);
    }
}

TEST(GeneratedPacketSchemaTest, PacketRoundTripIncludesForwardReferencedNestedTypes)
{
    GeneratedEnvelopeReq source;
    source.sequence = 42;
    source.user = MakeUser();
    source.users = {source.user, source.user};
    source.byId = {{10, source.user}, {20, source.user}};
    source.lookup = {{30, source.user}};
    source.history = {source.user};
    source.emptyValues.resize(3);
    NetBuffer buffer;
    source.PacketToBuffer(buffer);
    GeneratedEnvelopeReq result;
    result.BufferToPacket(buffer);
    EXPECT_EQ(result.GetPacketId(), static_cast<PacketId>(GeneratedSchemaPacketId::GENERATED_ENVELOPE_REQ));
    EXPECT_EQ(result.sequence, 42);
    ExpectUser(result.user, source.user);
    ASSERT_EQ(result.users.size(), 2);
    ExpectUser(result.users[1], source.user);
    ASSERT_EQ(result.byId.size(), 2);
    EXPECT_EQ(result.byId.begin()->first, 20);
    ExpectUser(result.byId.at(10), source.user);
    ExpectUser(result.lookup.at(30), source.user);
    ASSERT_EQ(result.history.size(), 1);
    ExpectUser(result.history.front(), source.user);
    EXPECT_EQ(result.emptyValues.size(), 3);
    EXPECT_EQ(buffer.GetUseSize(), 0);
}

TEST(GeneratedPacketSchemaTest, DirectOperatorsUseCodecForMutableConstAndTemporaryStructs)
{
    NetBuffer buffer;
    auto mutableUser = MakeUser();
    const auto constUser = mutableUser;
    buffer << mutableUser << constUser << MakeUser();
    for (int index = 0; index < 3; ++index)
    {
        GeneratedUser result;
        buffer >> result;
        ExpectUser(result, constUser);
    }
    EXPECT_EQ(buffer.GetUseSize(), 0);
    buffer.Init();
    GeneratedPosition position{3, 4};
    buffer << position;
    EXPECT_EQ(buffer.GetUseSize(), 2 * sizeof(float));
    float x{}, y{};
    buffer >> x >> y;
    EXPECT_FLOAT_EQ(x, 3);
    EXPECT_FLOAT_EQ(y, 4);
}

TEST(GeneratedPacketSchemaTest, FailedStructAndPacketReadsPreserveDestinations)
{
    NetBuffer buffer;
    buffer << static_cast<UINT64>(7) << std::string("partial") << 1.0f;
    auto user = MakeUser();
    EXPECT_ANY_THROW(buffer >> user);
    ExpectUser(user, MakeUser());
    buffer.Init();
    buffer << 5 << MakeUser() << static_cast<std::uint32_t>(1); // Truncated users vector.
    GeneratedEnvelopeReq packet;
    packet.sequence = 99;
    packet.user.nickname = "preserved";
    EXPECT_ANY_THROW(packet.BufferToPacket(buffer));
    EXPECT_EQ(packet.sequence, 99);
    EXPECT_EQ(packet.user.nickname, "preserved");
}

TEST(GeneratedPacketSchemaTest, EmptyStructContainersAndLegacyListCounts)
{
    NetBuffer buffer;
    std::vector<GeneratedEmpty> emptyValues(5);
    buffer << emptyValues;
    EXPECT_EQ(buffer.GetUseSize(), sizeof(std::uint32_t));
    std::vector<GeneratedEmpty> result;
    buffer >> result;
    EXPECT_EQ(result.size(), 5);
    EXPECT_EQ(buffer.GetUseSize(), 0);
    buffer.Init();
    std::list<GeneratedPosition> positions{{1, 2}, {3, 4}};
    buffer << positions;
    EXPECT_EQ(buffer.GetUseSize(), sizeof(size_t) + 4 * sizeof(float));
    std::list<GeneratedPosition> received;
    buffer >> received;
    ASSERT_EQ(received.size(), 2);
    EXPECT_FLOAT_EQ(received.back().y, 4);
    buffer.Init();
    buffer << static_cast<std::uint32_t>(BUFFFER_MAX + 1);
    EXPECT_THROW(buffer >> result, std::runtime_error);
    EXPECT_EQ(result.size(), 5);
}

TEST(GeneratedPacketSchemaTest, NestedListsAndLargeGeneratedFieldsUseCheckedStorage)
{
    NetBuffer buffer;
    auto user = MakeUser();
    user.nickname.assign(2000, 'x');
    buffer << user;
    GeneratedUser received;
    buffer >> received;
    ExpectUser(received, user);
    buffer.Init();
    std::vector<std::list<GeneratedPosition>> source{{{1, 2}}, {{3, 4}}};
    buffer << source;
    decltype(source) result;
    buffer >> result;
    ASSERT_EQ(result.size(), 2);
    ASSERT_EQ(result[1].size(), 1);
    EXPECT_FLOAT_EQ(result[1].front().y, 4);
}
