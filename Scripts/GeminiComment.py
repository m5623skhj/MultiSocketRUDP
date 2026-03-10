import os
import re
import json
import requests
import base64
from google import genai
from google.genai import types

MAX_ADDED_LINES = 800
REQUEST_TIMEOUT = 15

api_key      = os.environ["GEMINI_API_KEY"]
pr_number    = os.environ["PR_NUMBER"]
repo         = os.environ["REPO"]
github_token = os.environ["GITHUB_TOKEN"]
commit_sha   = os.environ["GITHUB_SHA"]

def gh_headers() -> dict:
    return {
        "Authorization": f"Bearer {github_token}",
        "Accept": "application/vnd.github+json"
    }


_file_cache: dict[str, str | None] = {}

def get_file_content(path: str) -> str | None:
    """파일 내용을 캐시 포함해서 반환. API 중복 호출 방지."""
    if path in _file_cache:
        return _file_cache[path]

    url  = f"https://api.github.com/repos/{repo}/contents/{path}?ref={commit_sha}"
    r    = requests.get(url, headers=gh_headers(), timeout=REQUEST_TIMEOUT)

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

    return any(p in region for p in ["@brief", "/**", "///", "// ----"])


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
    페이지네이션 처리하여 PR 인라인 코멘트 전체 반환.
    GitHub 기본 per_page=30이므로 전체 순회 필요.
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

def parse_added_lines(diff: str) -> list[dict]:
    """
    diff에서 추가된 라인(+ 로 시작)만 파일 기준 실제 라인 번호와 함께 추출.

    반환: [{"file": str, "line": int, "content": str}, ...]

    raw diff를 Gemini에게 그대로 주면 삭제된 라인이나 컨텍스트 라인을
    잘못 판단하는 문제가 있으므로, 추가 라인만 구조화해서 전달한다.
    """
    results: list[dict] = []
    current_file: str | None = None
    new_line_num = 0

    for line in diff.splitlines():
        if line.startswith("+++ b/"):
            current_file = line[6:].strip()
            new_line_num = 0

        elif line.startswith("@@ "):
            m = re.search(r"\+(\d+)", line)
            if m:
                new_line_num = int(m.group(1)) - 1

        elif line.startswith("+") and not line.startswith("+++"):
            new_line_num += 1
            if current_file:
                results.append({
                    "file":    current_file,
                    "line":    new_line_num,
                    "content": line[1:]
                })

        elif not line.startswith("-"):
            new_line_num += 1

    return results


def format_additions_for_prompt(additions: list[dict]) -> str:
    """
    추가된 라인을 파일별로 묶어 읽기 좋은 형태로 변환.
    예:
        === src/Foo.cpp ===
        42: int* p = GetPointer();
        43: p->DoSomething();
    """
    grouped: dict[str, list[dict]] = {}
    for a in additions:
        grouped.setdefault(a["file"], []).append(a)

    parts: list[str] = []
    for file_path, lines in grouped.items():
        block = f"=== {file_path} ===\n"
        block += "\n".join(f"{a['line']}: {a['content']}" for a in lines)
        parts.append(block)

    return "\n\n".join(parts)

def extract_json(text: str) -> str | None:
    """
    Gemini 응답에서 JSON 배열만 추출.
    마크다운 코드블록(```json ... ```) 도 처리.
    실패 시 전체 텍스트에서 [ ... ] 로 fallback.
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

additions = parse_added_lines(diff)

if not additions:
    set_status("success", "No added lines to review")
    exit(0)

if len(additions) > MAX_ADDED_LINES:
    print(f"[diff] 추가 라인 {len(additions)}개 — 상한 {MAX_ADDED_LINES} 초과, skip")
    set_status("success", "Diff too large - skipped")
    exit(0)

additions_text = format_additions_for_prompt(additions)
print(f"[diff] 추가 라인 {len(additions)}개 파싱 완료")

prompt = f"""
다음은 이번 PR에서 **새로 추가된 코드**입니다.
각 블록은 파일 경로와 함께 "라인번호: 코드" 형식으로 구성되어 있습니다.
이 코드들만 리뷰하세요.

{additions_text}
"""

system_instruction_prompt = """
당신은 매우 엄격한 시니어 코드 리뷰어입니다.

반드시 JSON 배열만 출력하세요. 마크다운 코드블록, 설명, 인사말을 절대 출력하지 마세요.

각 항목 필드:
  file     : 파일 경로 (문자열, 입력에서 그대로 사용)
  line     : 파일 기준 실제 줄 번호 (정수, 입력에서 제공된 번호 사용)
  comment  : 리뷰 내용 (한국어, 두 문장 이하)
  severity : "critical" 또는 "warning"

────────────────────────────────────────
critical 판단 기준 — 아래 경우만 사용하세요:

  1. 컴파일/링크 오류가 확실한 코드
     예) 존재하지 않는 함수 호출, 타입 불일치로 컴파일 불가
  2. null/nullptr 역참조가 확실한 경우
     예) null 체크 없이 포인터를 즉시 역참조
  3. use-after-free, double-free, 메모리 corruption
  4. 무한 루프 (탈출 조건이 코드상 절대 성립 불가)
  5. deadlock (락 획득 순서가 항상 교착 상태를 유발)
  6. 데이터 손실이 확실한 로직 오류
     예) 저장 전 덮어쓰기, 조건 반전으로 항상 삭제

"가능성이 있다", "경우에 따라", "특정 상황에서" → 전부 warning
────────────────────────────────────────

warning 예시:
  - 예외 처리 누락 가능성
  - 잠재적 성능 문제
  - 네이밍, 스타일, 가독성
  - 주석 누락
  - 경계값 검사 누락 가능성

출력 예시:
[
  {
    "file": "src/Foo.cpp",
    "line": 42,
    "comment": "ptr이 nullptr인 경우 역참조가 발생합니다. null 체크가 없습니다.",
    "severity": "critical"
  },
  {
    "file": "src/Bar.cpp",
    "line": 17,
    "comment": "함수명이 동작을 명확히 설명하지 않습니다. ProcessInput 등으로 변경을 고려하세요.",
    "severity": "warning"
  }
]

지적할 내용이 없으면 반드시 [] 만 출력하세요.
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
        print(f"[gemini] 비정상 종료: {finish_reason} — skip 처리")
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

valid_positions = {(a["file"], a["line"]) for a in additions}

current_keys   = set()
critical_found = False

for r in reviews:
    try:
        file_path    = str(r["file"])
        line         = int(r["line"])
        comment_body = str(r["comment"])
        severity     = str(r.get("severity", "warning")).lower()
    except (KeyError, ValueError, TypeError) as e:
        print(f"[skip] 파싱 실패: {e} / {r}")
        continue

    if severity not in ("critical", "warning"):
        severity = "warning"

    if (file_path, line) not in valid_positions:
        print(f"[skip] 추가되지 않은 라인 지적 무시: {file_path}:{line}")
        continue

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
