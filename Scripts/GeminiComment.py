import os
import json
import requests
from google import genai
from google.genai import types

MAX_LINES = 1000

api_key = os.environ["GEMINI_API_KEY"]
pr_number = os.environ["PR_NUMBER"]
repo = os.environ["REPO"]
github_token = os.environ["GITHUB_TOKEN"]
commit_sha = os.environ["GITHUB_SHA"]

def set_status(state, description):
    url = f"https://api.github.com/repos/{repo}/statuses/{commit_sha}"
    headers = {
        "Authorization": f"Bearer {github_token}",
        "Accept": "application/vnd.github+json"
    }
    data = {
        "state": state,
        "context": "ai-review-check",
        "description": description
    }
    requests.post(url, headers=headers, json=data)

with open("diff.txt", "r", encoding="utf-8", errors="ignore") as f:
    diff = f.read()

if not diff.strip():
    set_status("success", "No target file changes")
    exit(0)

line_count = diff.count("\n")
if line_count == 0:
    set_status("success", "No target file changes")
    exit(0)

if line_count > MAX_LINES:
    set_status("success", "Diff too large - skipped")
    exit(0)

if len(diff) > 20000:
    diff = diff[:20000]

prompt = f"""
다음 PR 변경 코드에서 어떤 기능들이 수정 되었는지 한국어로 간단한 리뷰를 작성하세요.
추가적으로 각 클래스 및 추가된 함수들에 대해서 주석이 달려 있지 않다면, 해당 기능에 대한 주석을 작성하세요.

{diff}
"""

system_instruction_prompt = """
당신은 고도로 훈련된 시니어 코드 리뷰 전문가입니다.
코드를 수정하지 말고 리뷰와 주석 제안만 작성하세요.
치명적 버그만 critical로 분류하세요.
일반 개선 사항은 warning으로 분류하세요.
출력은 JSON 배열만 사용하세요.
각 항목은 file, line, comment, severity 필드를 가져야 합니다.
severity는 critical 또는 warning만 사용하세요.
한국어만 사용하세요.
리뷰는 핵심만 간결하게 작성하세요.
각 코멘트는 주석이 아닌 경우, 최대 두 문장을 넘기지 마세요.
불필요한 서두, 인사말, 결론 문장을 작성하지 마세요.
중복 표현을 사용하지 마세요.
함수나 클래스에 대한 설명이 필요한 경우,
"주석을 추가해야 합니다" 같은 설명 문장을 작성하지 마세요.
반드시 실제 코드에 삽입 가능한 주석 형태로 작성하세요.

C++ / C# 함수 주석은 다음 형식을 사용하세요:

/**
 * @brief 함수의 역할 한 줄 요약
 * @param 매개변수 설명
 * @return 반환값 설명
 */

변수 또는 멤버는 다음 형식을 사용하세요:

/**
 * @brief 변수 역할 설명
 */

comment 필드에는 오직 주석 코드만 작성하세요.
설명 문장, 리뷰 문구, 마크다운 기호를 사용하지 마세요.
"""

client = genai.Client(api_key=api_key)

response = client.models.generate_content(
    model="gemini-2.5-flash",
    contents=prompt,
    config=types.GenerateContentConfig(
        system_instruction=system_instruction_prompt
    )
)

client.close()

if not response or not response.text:
    set_status("failure", "AI response empty")
    exit(1)

text = response.text.strip()
start = text.find("[")
end = text.rfind("]") + 1
json_text = text[start:end]

try:
    reviews = json.loads(json_text)
except Exception as e:
    print("JSON parse error:", e)
    set_status("failure", "AI JSON parse failed")
    exit(1)

if not isinstance(reviews, list):
    set_status("failure", "AI JSON invalid format")
    exit(1)

headers = {
    "Authorization": f"Bearer {github_token}",
    "Accept": "application/vnd.github+json"
}

critical_found = False

for r in reviews[:20]:
    try:
        body = r["comment"]
        body = f"```cpp\n{body}\n```"
        severity = r.get("severity", "warning")
        file_path = r["file"]
        line = r["line"]
    except:
        continue

    if severity == "critical":
        critical_found = True
        body = "🚨 " + body

    url = f"https://api.github.com/repos/{repo}/pulls/{pr_number}/comments"

    data = {
        "body": body,
        "commit_id": commit_sha,
        "path": file_path,
        "line": line,
        "side": "RIGHT"
    }

    resp = requests.post(url, headers=headers, json=data)
    print(resp.status_code, resp.text)

if critical_found:
    set_status("failure", "Critical issues detected by AI review")
else:
    set_status("success", "No critical issues found")
