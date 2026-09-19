// Compile with /Zs and one NETBUFFER_REJECT_* define. Each case must fail compilation.
// Intentionally excluded from CoreTest.vcxproj.
#include "PreCompile.h"
#include "NetServerSerializeBuffer.h"

void CheckRejectedContainer()
{
    NetBuffer buffer;
#if defined(NETBUFFER_REJECT_COMPARATOR)
    struct CustomCompare
    {
        bool operator()(int left, int right) const { return left < right; }
    };
    std::set<int, CustomCompare> values;
    buffer << values;
#elif defined(NETBUFFER_REJECT_STRUCT)
    struct Data { std::string text; };
    std::vector<Data> values;
    buffer << values;
#elif defined(NETBUFFER_REJECT_CONST_READ)
    const std::vector<int> values;
    buffer >> values;
#else
#error Select a NETBUFFER_REJECT_* case
#endif
}
