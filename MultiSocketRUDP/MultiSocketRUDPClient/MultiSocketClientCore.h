#pragma once

class MultiSocketClientCore
{
private:
	MultiSocketClientCore() = default;
	~MultiSocketClientCore() = default;
	MultiSocketClientCore& operator=(const MultiSocketClientCore&) = delete;
	MultiSocketClientCore(MultiSocketClientCore&&) = delete;

public:
	static MultiSocketClientCore& GetInst();
};