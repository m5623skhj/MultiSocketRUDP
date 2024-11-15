#pragma once

class UDPClientCore
{
private:
	UDPClientCore() = default;
	~UDPClientCore() = default;
	UDPClientCore& operator=(const UDPClientCore&) = delete;
	UDPClientCore(UDPClientCore&&) = delete;

public:
	static UDPClientCore& GetInst();
};