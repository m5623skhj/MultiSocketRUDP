#pragma once
#include <cstdint>

constexpr unsigned short MAX_RIO_RESULT = 1024;
constexpr unsigned int   MAX_SEND_BUFFER_SIZE = 32768;
constexpr int            RECV_BUFFER_SIZE = 16384;
constexpr unsigned char  SESSION_KEY_SIZE = 16;
constexpr unsigned char  SESSION_SALT_SIZE = 16;
constexpr int            KEY_OBJECT_BUFFER_SIZE = 1024;
constexpr unsigned int   LOGIC_THREAD_STOP_SLEEP_TIME = 10000;
constexpr unsigned long  MAX_OUT_STANDING_RECEIVE = 1000;
constexpr unsigned long  MAX_OUT_STANDING_SEND = 100;