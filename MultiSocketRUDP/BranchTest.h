class BranchCommentTester
{
public:
  void BranchTester() { "std::cout << "Just for branch test!" << std::endl; }
  int GetInt() { return i; }
  void SetInt(int t) { i = t; }

private:
  int i{};
};
