#pragma once

using PortType = unsigned short;
constexpr PortType invalidPortNumber = -1;

using SessionIdType = unsigned short;
constexpr SessionIdType invalidSessionId = -1;

using ThreadIdType = unsigned char;
using PacketSequence = unsigned long long;
const PacketSequence loginPacketSequence = 0;

using PacketRetransmissionCount = unsigned short;
const PacketRetransmissionCount maxPacketRetransmissionCount = 8;

constexpr unsigned short maxRIOResult = 256;
constexpr unsigned int maxSendBufferSize = 16384;
constexpr unsigned int workerThreadOneFrameMillisecond = 16;
constexpr int recvBufferSize = 8192;
constexpr unsigned char sessionKeySize = 16;
constexpr unsigned int logicThreadStopSleepTime = 10000;
constexpr unsigned char sessionInfoSize = 16 + sessionKeySize + sizeof(PortType) + sizeof(SessionIdType);
constexpr unsigned int retransmissionMillisecond = 50;
constexpr unsigned int retransmissionThreadSleepMillisecond = 16;

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
};
