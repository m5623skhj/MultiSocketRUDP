#pragma once

using PortType = unsigned short;
constexpr PortType invalidPortNumber = -1;

using SessionIdType = unsigned short;
constexpr SessionIdType invalidSessionId = -1;

using ThreadIdType = unsigned char;

#define MAX_RIO_RESULT 256
#define MAX_SEND_BUFFER_SIZE 16384
#define ONE_FRAME 10

enum class RIO_OPERATION_TYPE : INT8
{
	OP_ERROR = 0
	, OP_RECV
	, OP_SEND
	, OP_SEND_REQUEST
};
