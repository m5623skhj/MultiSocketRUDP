#pragma once
#include <cstdint>

using PortType = unsigned short;
using SessionIdType = unsigned short;
using ThreadIdType = unsigned char;
using PacketSequence = unsigned long long;
using PacketRetransmissionCount = unsigned short;

constexpr PortType      INVALID_PORT_NUMBER = static_cast<PortType>(-1);
constexpr SessionIdType INVALID_SESSION_ID = static_cast<SessionIdType>(-1);
constexpr PacketSequence LOGIN_PACKET_SEQUENCE = 0;