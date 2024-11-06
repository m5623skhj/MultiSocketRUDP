#pragma once

using PortType = unsigned short;
constexpr PortType invalidPortNumber = -1;

using SessionIdType = unsigned short;
constexpr SessionIdType invalidSessionId = -1;

using ThreadIdType = unsigned char;

constexpr unsigned short maxRIOResult = 256;
constexpr unsigned int maxSendBufferSize = 16384;
constexpr unsigned short oneFrame = 10;
constexpr int recvBufferSize = 8192;

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