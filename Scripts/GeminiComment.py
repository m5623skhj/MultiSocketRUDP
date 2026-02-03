import os
import requests
import google.generativeai as genai

MAX_LINES = 1000

api_key = os.environ["GEMINI_API_KEY"]
pr_number = os.environ["PR_NUMBER"]
repo = os.environ["REPO"]
github_token = os.environ["GITHUB_TOKEN"]
commit_sha = os.environ["GITHUB_SHA"]

genai.configure(api_key=api_key)
system_instruction = """
당신은 고도로 훈련된 시니어 코드 리뷰 전문가입니다.
코드를 수정하지 말고 리뷰와 주석 제안만 작성하세요.
치명적 버그만 critical로 분류하세요.
일반 개선 사항은 warning으로 분류하세요.
출력은 JSON 배열만 사용하세요.
각 항목은 file, line, comment, severity 필드를 가져야 합니다.
severity는 critical 또는 warning만 사용하세요.
한국어만 사용하세요.
리뷰는 핵심만 간결하게 작성하세요.
각 코멘트는 최대 두 문장을 넘기지 마세요.
불필요한 서두, 인사말, 결론 문장을 작성하지 마세요.
중복 표현을 사용하지 마세요.
"""

model = genai.GenerativeModel(
    model_name = "gemini-2.5-flash",
    system_instruction = system_instruction
)

with open("diff.txt", "r", encoding="utf-8", errors="ignore") as f:
    diff = f.read()

if not diff.strip():
    exit(0)

line_count = diff.count("\n")
if line_count == 0:
    print("No target file changes.")
    exit(0)

if line_count > MAX_LINES:
    print("Diff too large, skipping.")
    exit(0)

if len(diff) > 20000:
    diff = diff[:20000]

prompt = f"""
다음 PR 변경 코드에서 어떤 기능들이 수정 되었는지 한국어로 간단한 리뷰를 작성하세요.
추가적으로 각 클래스 및 추가된 함수들에 대해서 주석이 달려 있지 않다면, 해당 기능에 대한 주석을 작성하세요.

{diff}
"""

response = model.generate_content(prompt)
comment_body = response.text[:60000]

url = f"https://api.github.com/repos/{repo}/issues/{pr_number}/comments"
headers = {
    "Authorization": f"Bearer {github_token}",
    "Accept": "application/vnd.github+json"
}
data = {
    "body": comment_body
}

response = model.generate_content(prompt)
text = response.text.strip()

start = text.find("[")
end = text.rfind("]") + 1
json_text = text[start:end]

try:
    reviews = json.loads(json_text)
except:
    reviews = []

headers = {
    "Authorization": f"Bearer {github_token}",
    "Accept": "application/vnd.github+json"
}

critical_found = False

for r in reviews[:10]:
    body = r["comment"]
    severity = r.get("severity", "warning")

    if severity == "critical":
        critical_found = True
        body = "🚨 " + body

    url = f"https://api.github.com/repos/{repo}/pulls/{pr_number}/comments"

    data = {
        "body": body,
        "commit_id": commit_sha,
        "path": r["file"],
        "line": r["line"],
        "side": "RIGHT"
    }

    requests.post(url, headers=headers, json=data)

if critical_found:
    state = "failure"
    description = "Critical issues detected by AI review"
else:
    state = "success"
    description = "No critical issues found"

status_url = f"https://api.github.com/repos/{repo}/statuses/{commit_sha}"

status_data = {
    "state": state,
    "context": "ai-review-check",
    "description": description
}

requests.post(status_url, headers=headers, json=status_data)
