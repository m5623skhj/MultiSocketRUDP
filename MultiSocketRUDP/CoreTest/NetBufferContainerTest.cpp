#include "PreCompile.h"
#include <gtest/gtest.h>
#include "NetServerSerializeBuffer.h"

namespace
{
    template<typename Container>
    void CheckRoundTrip(const Container& source)
    {
        NetBuffer buffer;
        Container result;
        buffer << source;
        buffer >> result;
        EXPECT_EQ(result, source);
        EXPECT_EQ(buffer.GetUseSize(), 0);
    }
}

TEST(NetBufferContainerTest, AllContainersRoundTripIncludingNestedValues)
{
    CheckRoundTrip(std::vector<int>{1, -2, 3});
    CheckRoundTrip(std::vector<bool>{true, false, true});
    CheckRoundTrip(std::set<std::string>{"a", "b"});
    CheckRoundTrip(std::set<int, std::greater<int>>{1, 3, 2});
    CheckRoundTrip(std::map<int, std::string>{{1, "one"}, {2, "two"}});
    CheckRoundTrip(std::map<int, std::string, std::greater<int>>{{1, "one"}, {2, "two"}});
    CheckRoundTrip(std::unordered_map<int, std::vector<int>>{{7, {1, 2}}, {8, {3}}});
    CheckRoundTrip(std::unordered_set<std::string>{"alpha", "beta"});
    CheckRoundTrip(std::vector<std::map<int, std::vector<std::wstring>>>{{{1, {L"hello", L"world"}}}});
    CheckRoundTrip(std::vector<std::set<int>>{{1, 2}, {3}});
}

TEST(NetBufferContainerTest, EmptyContainersReplaceExistingContents)
{
    CheckRoundTrip(std::vector<int>{});
    CheckRoundTrip(std::set<int>{});
    CheckRoundTrip(std::map<int, int>{});
    CheckRoundTrip(std::unordered_set<int>{});
    CheckRoundTrip(std::unordered_map<int, int>{});
    NetBuffer buffer;
    std::vector<int> source;
    std::vector<int> result{5};
    buffer << source; // Mutable lvalue must not select the raw-copy fallback.
    EXPECT_EQ(buffer.GetUseSize(), 4);
    buffer >> result;
    EXPECT_TRUE(result.empty());
}

TEST(NetBufferContainerTest, OrderedWireIncludesDirectionCountAndSortedKeys)
{
    NetBuffer buffer;
    std::map<int, int, std::greater<int>> source{{1, 10}, {2, 20}};
    buffer << source;
    BYTE order{};
    std::uint32_t count{};
    int key{}, value{};
    buffer >> order >> count >> key >> value;
    EXPECT_EQ(order, 1);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(key, 2);
    EXPECT_EQ(value, 20);
    buffer >> key >> value;
    EXPECT_EQ(key, 1);
    EXPECT_EQ(value, 10);
    EXPECT_EQ(buffer.GetUseSize(), 0);
}

TEST(NetBufferContainerTest, RejectsOrderingMismatchAndInvalidDirection)
{
    NetBuffer buffer;
    buffer << std::set<int, std::greater<int>>{1, 2};
    std::set<int> result{99};
    EXPECT_THROW(buffer >> result, std::runtime_error);
    EXPECT_EQ(result, (std::set<int>{99}));
    EXPECT_EQ(buffer.GetBufferError(), 1);
    buffer.Init();
    buffer << static_cast<BYTE>(2) << static_cast<std::uint32_t>(0);
    EXPECT_THROW(buffer >> result, std::runtime_error);
    buffer.Init();
    buffer << std::map<int, int>{{1, 2}};
    std::map<int, int, std::greater<int>> descending{{99, 100}};
    EXPECT_THROW(buffer >> descending, std::runtime_error);
    EXPECT_EQ(descending.at(99), 100);
}

TEST(NetBufferContainerTest, RejectsDuplicateKeysAndElements)
{
    NetBuffer buffer;
    buffer << static_cast<BYTE>(0) << static_cast<std::uint32_t>(2) << 1 << 1;
    std::set<int> ordered{99};
    EXPECT_THROW(buffer >> ordered, std::runtime_error);
    EXPECT_EQ(ordered, (std::set<int>{99}));
    buffer.Init();
    buffer << static_cast<std::uint32_t>(2) << 1 << 10 << 1 << 20;
    std::unordered_map<int, int> mapping{{99, 100}};
    EXPECT_THROW(buffer >> mapping, std::runtime_error);
    EXPECT_EQ(mapping, (std::unordered_map<int, int>{{99, 100}}));
    buffer.Init();
    buffer << static_cast<std::uint32_t>(2) << 1 << 1;
    std::unordered_set<int> unordered{99};
    EXPECT_THROW(buffer >> unordered, std::runtime_error);
    EXPECT_EQ(unordered, (std::unordered_set<int>{99}));
}

TEST(NetBufferContainerTest, RejectsTruncationAndOversizedCountWithoutChangingDestination)
{
    NetBuffer buffer;
    buffer << static_cast<std::uint32_t>(2) << 42;
    std::vector<int> result{99};
    EXPECT_ANY_THROW(buffer >> result);
    EXPECT_EQ(result, (std::vector<int>{99}));
    buffer.Init();
    buffer << static_cast<std::uint32_t>(0xffffffffu);
    EXPECT_THROW(buffer >> result, std::runtime_error);
    EXPECT_EQ(result, (std::vector<int>{99}));
    buffer.Init();
    buffer << static_cast<std::uint32_t>(1) << static_cast<BYTE>(2);
    std::vector<bool> flags{true};
    EXPECT_THROW(buffer >> flags, std::runtime_error);
    EXPECT_EQ(flags, (std::vector<bool>{true}));
}

TEST(NetBufferContainerTest, GrowsStorageAndEnforcesPacketCapacity)
{
    CheckRoundTrip(std::vector<int>(1000, 42)); // Exceeds the initial 1024-byte allocation.
    CheckRoundTrip(std::vector<char>(BUFFFER_MAX - df_HEADER_SIZE - sizeof(std::uint32_t), 'x'));
    NetBuffer buffer;
    EXPECT_THROW(buffer << std::vector<char>(BUFFFER_MAX, 'x'), std::runtime_error);
    EXPECT_EQ(buffer.GetBufferError(), 2);
    buffer.Init();
    EXPECT_THROW(buffer << std::vector<std::string>{std::string(65536, 'x')}, std::runtime_error);
}

TEST(NetBufferContainerTest, PreservesUnorderedPolicyAndLegacyListFormat)
{
    NetBuffer buffer;
    buffer << std::unordered_set<int>{1, 2};
    std::unordered_set<int> result{99};
    result.max_load_factor(0.5f);
    buffer >> result;
    EXPECT_EQ(result, (std::unordered_set<int>{1, 2}));
    EXPECT_FLOAT_EQ(result.max_load_factor(), 0.5f);
    buffer.Init();
    std::list<int> legacy{1, 2};
    buffer << legacy;
    EXPECT_EQ(buffer.GetUseSize(), sizeof(size_t) + 2 * sizeof(int));
    std::list<int> received;
    buffer >> received;
    EXPECT_EQ(received, legacy);

    buffer.Init();
    std::list<std::string> strings{"one", "two"};
    std::list<std::wstring> wideStrings{L"three", L"four"};
    buffer << strings << wideStrings;
    EXPECT_EQ(buffer.GetUseSize(), 2 * sizeof(size_t) + 4 * sizeof(WORD) + 6 + 9 * sizeof(wchar_t));
    std::list<std::string> receivedStrings{"existing"};
    std::list<std::wstring> receivedWideStrings{L"existing"};
    buffer >> receivedStrings >> receivedWideStrings;
    EXPECT_EQ(receivedStrings, (std::list<std::string>{"existing", "one", "two"}));
    EXPECT_EQ(receivedWideStrings, (std::list<std::wstring>{L"existing", L"three", L"four"}));
    EXPECT_EQ(buffer.GetUseSize(), 0);
}
