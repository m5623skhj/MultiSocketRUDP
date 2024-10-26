#pragma once

using PortType = unsigned short;
constexpr PortType invalidPortNumber = -1;

using SessionIdType = unsigned short;
constexpr SessionIdType invalidSessionId = -1;

using ThreadIdType = unsigned char;

#define MAX_RIO_RESULT 256
#define MAX_SEND_BUFFER_SIZE 16384
#define ONE_FRAME 10

