#pragma once

enum class PACKET_ID : unsigned int
{
	INVALID_PACKET_ID = 0
	, PING
	, PONG
	, TEST_STRING_PACKET_REQ
	, TEST_STRING_PACKET_RES
	, TEST_PACKET_REQ
	, TEST_PACKET_RES
};