#pragma once

enum class PACKET_ID : unsigned int
{
	InvalidPacketId = 0
	, Ping
	, Pong
	, TestStringPacket
	, TestPacketReq
	, TestPacketRes
};