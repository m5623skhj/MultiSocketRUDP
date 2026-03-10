import os
import json
import requests
import base64
from google import genai
from google.genai import types

MAX_LINES       = 1000
REQUEST_TIMEOUT = 15

api_key      = os.environ["GEMINI_API_KEY"]
pr_number    = os.environ["PR_NUMBER"]
repo         = os.environ["REPO"]
github_token = os.environ["GITHUB_TOKEN"]
commit_sha   = os.environ["GITHUB_SHA"]

def gh_headers():
    return {
        "Authorization": f"Bearer {github_token}",
        "Accept": "application/vnd.github+json"
    }


_file_cache: dict[str, str | None] = {}

def get_file_content(path: str) -> str | None:
    if path in _file_cache:
        return _file_cache[path]

    url = f"https://api.github.com/repos/{repo}/contents/{path}?ref={commit_sha}"
    r   = requests.get(url, headers=gh_headers(), timeout=REQUEST_TIMEOUT)

    if r.status_code != 200:
        _file_cache[path] = None
        return None

    data = r.json()
    if "content" not in data:
        _file_cache[path] = None
        return None

    content = base64.b64decode(data["content"]).decode("utf-8", errors="ignore")
    _file_cache[path] = content
    return content


def has_existing_comment(file_path: str, line: int) -> bool:
    """해당 라인 근방에 이미 사람이 작성한 주석이 있으면 True."""
    content = get_file_content(file_path)
    if not content:
        return False

    lines  = content.splitlines()
    start  = max(0, line - 6)
    end    = min(len(lines), line + 1)
    region = "\n".join(lines[start:end])

    comment_patterns = ["@brief", "/**", "///", "// ----"]
    return any(p in region for p in comment_patterns)


def approve_pr() -> None:
    url  = f"https://api.github.com/repos/{repo}/pulls/{pr_number}/reviews"
    data = {"event": "APPROVE", "body": "✅ AI review passed. No critical issues found."}
    resp = requests.post(url, headers=gh_headers(), json=data, timeout=REQUEST_TIMEOUT)
    print(f"[approve] HTTP {resp.status_code}")
    if resp.status_code not in (200, 201):
        print(f"[approve] 실패: {resp.text[:200]}")


def set_status(state: str, description: str) -> None:
    """state: 'success' | 'failure' | 'pending'"""
    url  = f"https://api.github.com/repos/{repo}/statuses/{commit_sha}"
    data = {
        "state":       state,
        "context":     "ai-review/gemini",
        "description": description[:140]
    }
    resp = requests.post(url, headers=gh_headers(), json=data, timeout=REQUEST_TIMEOUT)
    print(f"[status] {state}: {description} → HTTP {resp.status_code}")


def get_all_pr_comments() -> list:
    """
    페이지네이션을 처리하여 PR의 모든 인라인 코멘트를 반환.
    GitHub 기본 per_page=30이므로 전체를 순회해야 중복 코멘트를 방지할 수 있음.
    """
    comments: list = []
    page = 1
    while True:
        url  = f"https://api.github.com/repos/{repo}/pulls/{pr_number}/comments"
        resp = requests.get(
            url,
            headers=gh_headers(),
            params={"per_page": 100, "page": page},
            timeout=REQUEST_TIMEOUT
        )
        if resp.status_code != 200:
            print(f"[comments] 조회 실패 HTTP {resp.status_code}")
            break
        batch = resp.json()
        if not batch:
            break
        comments.extend(batch)
        if len(batch) < 100:
            break
        page += 1
    print(f"[comments] 총 {len(comments)}개 조회")
    return comments


def extract_json(text: str) -> str | None:
    """
    Gemini 응답에서 JSON 배열만 추출.
    마크다운 코드블록(```json ... ```) 도 처리.
    코드블록 파싱 실패 시 전체 텍스트에서 [ ... ] 로 fallback.
    """
    text = text.strip()

    if "```" in text:
        for part in text.split("```"):
            part = part.strip()
            if part.startswith("json"):
                part = part[4:].strip()
            if part.startswith("["):
                s = part.find("[")
                e = part.rfind("]") + 1
                if s != -1 and e > 0:
                    return part[s:e]

    s = text.find("[")
    e = text.rfind("]") + 1
    if s == -1 or e == 0:
        return None
    return text[s:e]


with open("diff.txt", "r", encoding="utf-8", errors="ignore") as f:
    diff = f.read()

if not diff.strip():
    set_status("success", "No target file changes")
    exit(0)

if diff.count("\n") > MAX_LINES:
    set_status("success", "Diff too large - skipped")
    exit(0)

if len(diff) > 20000:
    print(f"[diff] 20000자 초과로 잘림 (원본 {len(diff)}자)")
    diff = diff[:20000]

prompt = f"""
다음은 PR의 변경 diff입니다. 새로 추가되거나 수정된 코드(+ 로 시작하는 라인)만 검토하세요.
삭제된 라인(- 로 시작하는 라인)은 절대 지적하지 마세요.

{diff}
"""

system_instruction_prompt = """
당신은 매우 엄격한 시니어 코드 리뷰어입니다.

반드시 JSON 배열만 출력하세요. 다른 텍스트, 설명, 마크다운 코드블록은 출력하지 마세요.

각 항목 필드:
  file     : 파일 경로 (문자열)
  line     : 파일 기준 실제 줄 번호 (정수, diff position 아님)
  comment  : 리뷰 내용 (두 문장 이하, 한국어)
  severity : "critical" 또는 "warning"

────────────────────────────
critical 은 아래 경우만 사용하세요:
  - 컴파일/런타임 오류가 확실한 코드
  - 확실한 null 역참조 (null check 없이 사용)
  - use-after-free, 메모리 corruption
  - 무한 루프, deadlock
  - 데이터 손실을 일으키는 명백한 로직 오류

그 외 모든 것은 warning 으로 분류하세요:
  - 스타일, 네이밍, 가독성
  - 주석 누락
  - 잠재적(가능성만 있는) 문제
  - 성능 개선 제안
────────────────────────────

출력 예시:
[
  {"file": "src/foo.cpp", "line": 42, "comment": "p가 nullptr일 때 역참조됩니다.", "severity": "critical"},
  {"file": "src/bar.cpp", "line": 10, "comment": "변수명이 너무 짧습니다.", "severity": "warning"}
]

지적할 내용이 없으면 빈 배열 [] 을 출력하세요.
인사말, 서두, 결론 문장을 절대 작성하지 마세요.
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

try:
    finish_reason = response.candidates[0].finish_reason
    print(f"[gemini] finish_reason={finish_reason}")
    if str(finish_reason) not in ("FinishReason.STOP", "STOP", "1"):
        print(f"[gemini] 비정상 종료: {finish_reason} — 응답이 불완전할 수 있어 skip 처리")
        set_status("success", "AI response truncated - skipped")
        exit(0)
except Exception:
    pass

print("[gemini raw]", response.text[:500])

json_text = extract_json(response.text)

if not json_text:
    set_status("failure", "AI JSON parse failed")
    exit(1)

try:
    reviews = json.loads(json_text)
except json.JSONDecodeError as e:
    print(f"[json error] {e}\nraw: {json_text[:300]}")
    set_status("failure", "AI JSON invalid")
    exit(1)

if not isinstance(reviews, list):
    set_status("failure", "AI JSON invalid format")
    exit(1)

MARKER = "<!-- GEMINI_INLINE_REVIEW -->"

existing_comments = get_all_pr_comments()

ai_existing = {
    (c["path"], c.get("line")): c
    for c in existing_comments
    if MARKER in c.get("body", "")
}

current_keys   = set()
critical_found = False

for r in reviews:
    try:
        file_path    = str(r["file"])
        line         = int(r["line"])
        comment_body = str(r["comment"])
        severity     = str(r.get("severity", "warning")).lower()
    except (KeyError, ValueError, TypeError) as e:
        print(f"[skip] 항목 파싱 실패: {e} / {r}")
        continue

    if severity not in ("critical", "warning"):
        severity = "warning"

    print(f"[review] {severity.upper():8s} {file_path}:{line}  {comment_body[:80]}")

    if has_existing_comment(file_path, line):
        print(f"  → 기존 주석 있음, 생략")
        continue

    key = (file_path, line)
    current_keys.add(key)

    if severity == "critical":
        critical_found = True
        body = f"{MARKER}\n### 🚨 Critical\n{comment_body}"
    else:
        body = f"""{MARKER}
<details>
<summary>⚠️ Warning</summary>

{comment_body}

</details>"""

    if key in ai_existing:
        update_url = f"https://api.github.com/repos/{repo}/pulls/comments/{ai_existing[key]['id']}"
        resp = requests.patch(update_url, headers=gh_headers(), json={"body": body}, timeout=REQUEST_TIMEOUT)
        print(f"  → PATCH HTTP {resp.status_code}")
    else:
        create_url = f"https://api.github.com/repos/{repo}/pulls/{pr_number}/comments"
        data = {
            "body":      body,
            "commit_id": commit_sha,
            "path":      file_path,
            "line":      line,
            "side":      "RIGHT"
        }
        resp = requests.post(create_url, headers=gh_headers(), json=data, timeout=REQUEST_TIMEOUT)
        print(f"  → POST HTTP {resp.status_code}")
        if resp.status_code not in (200, 201):
            print(f"  → 생성 실패: {resp.text[:200]}")

for key, comment in ai_existing.items():
    if key not in current_keys:
        delete_url = f"https://api.github.com/repos/{repo}/pulls/comments/{comment['id']}"
        resp = requests.delete(delete_url, headers=gh_headers(), timeout=REQUEST_TIMEOUT)
        print(f"[delete] 오래된 AI 코멘트 삭제 {key} → HTTP {resp.status_code}")

if critical_found:
    set_status("failure", "Critical issues detected by AI review")
else:
    set_status("success", "No critical issues found")
    approve_pr()
