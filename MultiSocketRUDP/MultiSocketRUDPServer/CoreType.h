#pragma once

using PortType = unsigned short;
constexpr PortType INVALID_PORT_NUMBER = -1;

using SessionIdType = unsigned short;
constexpr SessionIdType INVALID_SESSION_ID = -1;

using ThreadIdType = unsigned char;
using PacketSequence = unsigned long long;
constexpr PacketSequence LOGIN_PACKET_SEQUENCE = 0;

using PacketRetransmissionCount = unsigned short;

constexpr unsigned short MAX_RIO_RESULT = 1024;
constexpr unsigned int MAX_SEND_BUFFER_SIZE = 32768;
constexpr int RECV_BUFFER_SIZE = 16384;
constexpr unsigned char SESSION_KEY_SIZE = 16;
constexpr unsigned char SESSION_SALT_SIZE = 16;
constexpr unsigned int LOGIC_THREAD_STOP_SLEEP_TIME = 10000;

#pragma region RIO
constexpr ULONG MAX_OUT_STANDING_RECEIVE = 1000;
constexpr ULONG MAX_OUT_STANDING_SEND = 100;
#pragma endregion RIO

#define OUT

enum class RIO_OPERATION_TYPE : INT8
{
	OP_ERROR = 0
	, OP_RECV
	, OP_SEND
};

enum class PACKET_TYPE : unsigned char
{
	INVALID_TYPE = 0
	, CONNECT_TYPE
	, DISCONNECT_TYPE
	, SEND_TYPE
	, SEND_REPLY_TYPE
	, HEARTBEAT_TYPE
	, HEARTBEAT_REPLY_TYPE
};

enum class CONNECT_RESULT_CODE : unsigned char
{
	SUCCESS = 0
	, SERVER_FULL
	, ALREADY_CONNECTED_SESSION
	, CREATE_SOCKET_FAILED
	, RIO_INIT_FAILED
	, DO_RECV_FAILED
	, SESSION_KEY_GENERATION_FAILED
};

enum class PACKET_DIRECTION : uint8_t
{
	CLIENT_TO_SERVER = 0,
	CLIENT_TO_SERVER_REPLY = 1,
	SERVER_TO_CLIENT = 2,
	SERVER_TO_CLIENT_REPLY = 3,
	INVALID = 255
};

struct TickSet
{
	UINT64 nowTick = 0;
};
