#pragma once
#include <vector>

template <typename T>
class RingBuffer
{
public:
	explicit RingBuffer(const unsigned short inCapacity)
		: buffer(inCapacity)
		, capacity(inCapacity)
	{
	}

	void Resize(const unsigned short newCapacity)
	{
		buffer.resize(newCapacity);
		capacity = newCapacity;
		head = 0;
		tail = 0;
		count = 0;
	}

	bool Push(const T& item)
	{
		if (IsFull())
		{
			return false;
		}

		buffer[tail] = item;
		tail = (tail + 1) % capacity;
		++count;

		return true;
	}

	bool Pop(OUT T& item)
	{
		if (IsEmpty())
		{
			return false;
		}

		item = buffer[head];
		head = (head + 1) % capacity;
		--count;
		return true;
	}

	[[nodiscard]]
	const T& Front() const
	{
		return buffer[head];
	}

	[[nodiscard]]
	bool IsEmpty() const noexcept
	{ 
		return count == 0; 
	}

	[[nodiscard]]
	bool IsFull() const noexcept
	{ 
		return count == capacity;
	}

	[[nodiscard]]
	unsigned short GetCount() const noexcept
	{ 
		return count;
	}

	void Clear()
	{
		head = 0;
		tail = 0;
		count = 0;
	}

private:
	std::vector<T> buffer;
	unsigned short capacity = 0;
	unsigned short head = 0;
	unsigned short tail = 0;
	unsigned short count = 0;
};