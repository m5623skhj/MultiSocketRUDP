#pragma once

/*
* ㅗㅗㅗㅗㅗ
*/
class commentTester
{
public:
    commentTester() = default;
    // 메롱
    // 이건 주석 테스트 용이란다
    // 피곤하다
    ~commentTester() = default;

public:
    int GetItem() { return item; }
    void SetItem(int inItem) { item = inItem; }

private:
    int item{};
};
