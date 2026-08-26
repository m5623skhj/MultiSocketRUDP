#pragma once

#include <thread>
#include <map>
#include <vector>
#include <functional>

enum class THREAD_GROUP : uint8_t;

// ----------------------------------------
// @brief 서버 작업 종류별 std::jthread 그룹의 시작과 종료를 관리합니다.
// 각 스레드에는 그룹 내부 인덱스와 stop_token이 전달되며 소멸 시 모든 그룹의 종료를 요청하고 join합니다.
// Start/Stop 호출 자체는 동기화되지 않으므로 서버 생명주기 제어 스레드에서 직렬로 호출해야 합니다.
// ----------------------------------------
class RUDPThreadManager
{
public:
	RUDPThreadManager() = default;
	~RUDPThreadManager();

public:
	// ----------------------------------------
	// @brief 중복되지 않은 작업 그룹에 지정된 수의 스레드를 생성합니다.
	// @param threadFunction stop_token과 그룹 내부 스레드 인덱스를 받는 작업 함수입니다.
	// ----------------------------------------
	void StartThreads(const THREAD_GROUP threadGroup, std::function<void(std::stop_token, unsigned char)> threadFunction, const uint8_t numOfThreads);
	// ----------------------------------------
	// @brief 한 그룹의 모든 스레드에 종료를 요청하고 join한 뒤 그룹을 제거합니다.
	// ----------------------------------------
	void StopThreadGroup(const THREAD_GROUP threadGroup);
	// ----------------------------------------
	// @brief 등록된 모든 그룹의 스레드에 종료를 요청하고 join합니다.
	// ----------------------------------------
	void StopAllThreads();

private:
	std::map<THREAD_GROUP, std::vector<std::jthread>> threadGroups;
};
