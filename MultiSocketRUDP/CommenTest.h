#pragma once

// ----------------------------------------
// @brief 주석 테스트 및 기본적인 멤버 변수를 포함하는 클래스
// ----------------------------------------
class commentTester
{
public:
    commentTester() = default;
    ~commentTester() = default;

public:
    // ----------------------------------------
    // @brief 내부 item 값을 반환합니다.
    // @return 현재 item 값
    // ----------------------------------------
    int GetItem() { return item; }
    // ----------------------------------------
    // @brief 내부 item 값을 설정합니다.
    // @param inItem 설정할 새로운 item 값
    // ----------------------------------------
    void SetItem(int inItem) { item = inItem; }

    void PrintString() { std::cout << "이건 컴파일 에러를 발생시키지롱!" << 그러나! 갑자기 컴파일을 일으켜 버리는데! << "std::endl; }

private:
    // ----------------------------------------
    // @brief 테스트용 정수 값
    // ----------------------------------------
    int item{};
};
