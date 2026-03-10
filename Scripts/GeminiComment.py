import os
import json
import requests
from google import genai
from google.genai import types

def get_file_content(path):
    url = f"https://api.github.com/repos/{repo}/contents/{path}?ref={commit_sha}"
    headers = {
        "Authorization": f"Bearer {github_token}",
        "Accept": "application/vnd.github+json"
    }
    r = requests.get(url, headers=headers)
    if r.status_code != 200:
        return None
    data = r.json()
    if "content" not in data:
        return None
    import base64
    return base64.b64decode(data["content"]).decode("utf-8", errors="ignore")

def has_existing_comment(file_path, line):
    content = get_file_content(file_path)
    if not content:
        return False
    lines = content.splitlines()
    start = max(0, line - 6)
    end = min(len(lines), line + 1)
    region = "\n".join(lines[start:end])
    comment_patterns = ["@brief", "/**", "///", "// ----------------------------------------"]
    for p in comment_patterns:
        if p in region:
            return True
    return False

def approve_pr():
    url = f"https://api.github.com/repos/{repo}/pulls/{pr_number}/reviews"
    headers = {
        "Authorization": f"Bearer {github_token}",
        "Accept": "application/vnd.github+json"
    }
    data = {"event": "APPROVE", "body": "AI review passed."}
    requests.post(url, headers=headers, json=data)

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

MAX_LINES = 1000

api_key = os.environ["GEMINI_API_KEY"]
pr_number = os.environ["PR_NUMBER"]
repo = os.environ["REPO"]
github_token = os.environ["GITHUB_TOKEN"]
commit_sha = os.environ["GITHUB_SHA"]

with open("diff.txt", "r", encoding="utf-8", errors="ignore") as f:
    diff = f.read()

if not diff.strip():
    set_status("success", "No target file changes")
    exit(0)

if diff.count("\n") > MAX_LINES:
    set_status("success", "Diff too large - skipped")
    exit(0)

if len(diff) > 20000:
    diff = diff[:20000]

prompt = f"""
다음 PR 변경 코드에서 한국어로 간단한 리뷰를 작성하세요.
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
각 코멘트는 최대 두 문장을 넘기지 마세요.
불필요한 서두, 인사말, 결론 문장을 작성하지 마세요.
중복 표현을 사용하지 마세요.
함수나 클래스에 대한 설명이 필요한 경우,
반드시 실제 코드에 삽입 가능한 주석 형태로 작성하세요.
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
except:
    set_status("failure", "AI JSON parse failed")
    exit(1)

if not isinstance(reviews, list):
    set_status("failure", "AI JSON invalid format")
    exit(1)

headers = {
    "Authorization": f"Bearer {github_token}",
    "Accept": "application/vnd.github+json"
}

marker = "<!-- GEMINI_INLINE_REVIEW -->"

existing_url = f"https://api.github.com/repos/{repo}/pulls/{pr_number}/comments"
existing_resp = requests.get(existing_url, headers=headers)
existing_comments = existing_resp.json() if existing_resp.status_code == 200 else []

ai_existing = {
    (c["path"], c.get("line")): c
    for c in existing_comments
    if marker in c.get("body", "")
}

current_keys = set()
critical_found = False

for r in reviews:
    try:
        file_path = r["file"]
        line = r["line"]
        comment_body = r["comment"]
        severity = r.get("severity", "warning")
    except:
        continue

    if has_existing_comment(file_path, line):
        continue

    key = (file_path, line)
    current_keys.add(key)

    if severity == "critical":
        critical_found = True
        body = f"{marker}\n🚨\n{comment_body}"
    else:
        body = f"""{marker}
<details>
<summary>⚠ Warning</summary>

{comment_body}

</details>
"""

    if key in ai_existing:
        update_url = f"https://api.github.com/repos/{repo}/pulls/comments/{ai_existing[key]['id']}"
        requests.patch(update_url, headers=headers, json={"body": body})
    else:
        create_url = f"https://api.github.com/repos/{repo}/pulls/{pr_number}/comments"
        data = {
            "body": body,
            "commit_id": commit_sha,
            "path": file_path,
            "line": line,
            "side": "RIGHT"
        }
        requests.post(create_url, headers=headers, json=data)

for key, comment in ai_existing.items():
    if key not in current_keys:
        delete_url = f"https://api.github.com/repos/{repo}/pulls/comments/{comment['id']}"
        requests.delete(delete_url, headers=headers)

if critical_found:
    set_status("failure", "Critical issues detected by AI review")
else:
    set_status("success", "No critical issues found")
    approve_pr()
