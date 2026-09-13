#include <cstdlib>

// Deliberately invalid access: the runner must observe an ASan heap-buffer-overflow.
int main(int argc, char**)
{
	volatile int index = argc + 7;
	auto* buffer = static_cast<char*>(std::malloc(8));
	if (buffer == nullptr) return 2;
	buffer[index] = 42;
	std::free(buffer);
	return 0;
}
