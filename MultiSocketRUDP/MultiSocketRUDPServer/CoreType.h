#pragma once

using PortType = unsigned short;
constexpr PortType invalidPortNumber = -1;

using SessionIdType = unsigned short;
constexpr SessionIdType invalidSessionId = -1;

using ThreadIdType = unsigned char;
using PacketSequence = unsigned long long;
const PacketSequence loginPacketSequence = 0;

using PacketRetransmissionCount = unsigned short;

constexpr unsigned short maxRIOResult = 1024;
constexpr unsigned int maxSendBufferSize = 32768;
constexpr int recvBufferSize = 16384;
constexpr unsigned char sessionKeySize = 16;
constexpr unsigned int logicThreadStopSleepTime = 10000;

#pragma region RIO
constexpr ULONG maxOutStandingReceive = 1000;
constexpr ULONG maxOutStandingSend = 100;
#pragma endregion RIO

#define OUT

enum class RIO_OPERATION_TYPE : INT8
{
	OP_ERROR = 0
	, OP_RECV
	, OP_SEND
	, OP_SEND_REQUEST
};

enum class PACKET_TYPE : unsigned char
{
	InvalidType = 0
	, ConnectType
	, DisconnectType
	, SendType
	, SendReplyType
	, HeartbeatType
	, HeartbeatReplyType
};

struct TickSet
{
	UINT64 nowTick = 0;
	UINT64 beforeTick = 0;
};
