import os
import re
import json
import time
import logging
import requests
import base64
from google import genai

logging.basicConfig(
    level=logging.INFO,
    format="[%(levelname)s] %(message)s"
)
log = logging.getLogger("ai-review")

MAX_FUNCTION_LINES = 500
REQUEST_TIMEOUT = 15
BATCH_SIZE = 20
MAX_BATCH_CHARS = 20000
GEMINI_MAX_RETRIES = 3
RATE_LIMIT_BUFFER = 10
NON_RETRYABLE_STATUSES = {401, 403, 404, 409, 422}

MARKER = "<!-- GEMINI_FUNCTION_SUMMARY -->"
VALID_STATUSES = {"ok", "new", "improve"}

FENCE_MAP = {
    "cpp": "cpp",
    "csharp": "csharp",
    "python": "python"
}

_RE_STRIP = re.compile(
    r'"(?:[^"\\]|\\.)*"'
    r"|'(?:[^'\\]|\\.)*'"
    r'|/\*.*?\*/'
    r'|//.*$'
)
_RE_HUNK_HEADER = re.compile(r"@@ -\d+(?:,\d+)? \+(\d+)")
_RE_FUNC_NAME = re.compile(r'([~\w:<>]+)\s*\(')
_RE_CODE_FENCE = re.compile(r'```(?:json)?\s*')
_RE_FIRST_WORD = re.compile(r'(\w+)')

_CSHARP_MODIFIERS = frozenset({
    "public", "private", "protected", "internal",
    "static", "async", "override", "virtual", "abstract",
    "sealed", "extern", "partial", "new", "unsafe"
})

_CSHARP_RETURN_HINTS = frozenset({
    "void", "int", "string", "bool", "float", "double", "decimal",
    "long", "short", "byte", "char", "object", "var", "dynamic",
    "Task", "IEnumerable", "List", "Dictionary"
})

_CONTROL_KEYWORDS = frozenset({
    "if", "else", "for", "foreach", "while", "do",
    "switch", "catch", "finally", "lock", "using",
    "return", "sizeof", "typeof", "nameof"
})

MAX_CONSECUTIVE_BLANK_LINES = 1

api_key      = os.environ["GEMINI_API_KEY"]
pr_number    = os.environ["PR_NUMBER"]
repo         = os.environ["REPO"]
github_token = os.environ["GITHUB_TOKEN"]


def gh_headers():
    return {
        "Authorization": f"Bearer {github_token}",
        "Accept": "application/vnd.github+json"
    }


def check_rate_limit(response):
    remaining = response.headers.get("X-RateLimit-Remaining")
    reset_at  = response.headers.get("X-RateLimit-Reset")

    if remaining is None or reset_at is None:
        return

    try:
        remaining = int(remaining)
        reset_at  = int(reset_at)
    except ValueError:
        return

    if remaining <= RATE_LIMIT_BUFFER:
        wait = max(reset_at - int(time.time()), 1)
        log.info("GitHub rate limit low (%d left), waiting %ds", remaining, wait)
        time.sleep(wait)


def safe_request(method, url, **kwargs):
    for attempt in range(3):
        try:
            r = method(url, **kwargs)

            if r.status_code in (200, 201, 204):
                check_rate_limit(r)
                return r

            if r.status_code == 403 and "rate limit" in r.text.lower():
                reset_at = r.headers.get("X-RateLimit-Reset")
                if reset_at:
                    try:
                        wait = max(int(reset_at) - int(time.time()), 1)
                    except ValueError:
                        wait = 60
                    log.warning("Rate limited, waiting %ds", wait)
                    time.sleep(wait)
                    continue

            if r.status_code in NON_RETRYABLE_STATUSES:
                log.error("%s -> %d, not retrying", url, r.status_code)
                return None

            log.warning("%s -> %d, retry %d/3", url, r.status_code, attempt + 1)

        except requests.RequestException as e:
            log.warning("Request failed: %s, retry %d/3", e, attempt + 1)
            continue

    return None


def resolve_commit_sha():
    env_sha = os.environ.get("GITHUB_SHA", "")
    url = f"https://api.github.com/repos/{repo}/pulls/{pr_number}"
    r = safe_request(requests.get, url, headers=gh_headers(), timeout=REQUEST_TIMEOUT)

    if r:
        try:
            head_sha = r.json().get("head", {}).get("sha")
            if head_sha:
                return head_sha
        except (ValueError, KeyError):
            pass

    log.warning("Failed to fetch PR head SHA, falling back to GITHUB_SHA: %s", env_sha)
    return env_sha


_file_cache = {}


def get_file_content(path, commit_sha):
    if path in _file_cache:
        return _file_cache[path]

    url = f"https://api.github.com/repos/{repo}/contents/{path}?ref={commit_sha}"
    r = safe_request(requests.get, url, headers=gh_headers(), timeout=REQUEST_TIMEOUT)

    if not r:
        _file_cache[path] = None
        return None

    data = r.json()

    if "content" not in data:
        download_url = data.get("download_url")
        if download_url:
            r2 = safe_request(
                requests.get, download_url,
                headers=gh_headers(),
                timeout=REQUEST_TIMEOUT
            )
            if r2:
                content = r2.text
                _file_cache[path] = content
                return content
        log.warning("No content for %s (file too large or binary)", path)
        _file_cache[path] = None
        return None

    content = base64.b64decode(data["content"]).decode("utf-8", errors="ignore")
    _file_cache[path] = content
    return content


def parse_added_lines(diff):
    results = []
    current_file = None
    new_line_num = 0

    for line in diff.splitlines():
        if line.startswith("+++ b/"):
            current_file = line[6:].strip()
            new_line_num = 0
        elif line.startswith("@@ "):
            m = _RE_HUNK_HEADER.search(line)
            if m:
                new_line_num = int(m.group(1)) - 1
        elif line.startswith("+") and not line.startswith("+++"):
            new_line_num += 1
            if current_file:
                results.append({"file": current_file, "line": new_line_num})
        elif not line.startswith("-"):
            new_line_num += 1

    return results


def strip_strings_and_comments(line):
    return _RE_STRIP.sub('', line)


def count_braces(line):
    cleaned = strip_strings_and_comments(line)
    return cleaned.count('{'), cleaned.count('}')


def has_assignment_outside_parens(s):
    depth = 0
    i = 0
    while i < len(s):
        ch = s[i]
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        elif ch == '=' and depth == 0:
            if i + 1 < len(s) and s[i + 1] == '=':
                i += 2
                continue
            if i > 0 and s[i - 1] in ('!', '<', '>'):
                i += 1
                continue
            return True
        i += 1
    return False


def _is_keyword_start(s):
    """키워드 뒤에 공백/괄호/탭이 오는 경우만 True."""
    m = _RE_FIRST_WORD.match(s)
    if not m:
        return False
    first_word = m.group(1)
    if first_word not in _CONTROL_KEYWORDS:
        return False
    rest = s[len(first_word):]
    return rest == "" or rest[0] in (' ', '(', '\t')


def is_cpp_signature(line):
    s = line.strip()
    if not ("(" in s):
        return False
    if _is_keyword_start(s):
        return False
    if has_assignment_outside_parens(s):
        return False
    return True


def _track_braces(lines, start_line):
    """start_line부터 중괄호 균형이 맞는 지점까지 탐색. (found, end_line) 반환."""
    brace = 0
    found = False
    end = start_line

    while end < len(lines):
        o, c = count_braces(lines[end])
        brace += o
        brace -= c
        if o > 0:
            found = True
        if found and brace == 0:
            break
        end += 1

    return found, end


def extract_cpp_functions(lines):
    results = []
    i = 0
    while i < len(lines):
        stripped = lines[i].strip()

        if not is_cpp_signature(stripped):
            i += 1
            continue

        start = i

        # Case 1: 한 줄에 ( ) 모두 있음
        if "(" in stripped and ")" in stripped:
            found, end = _track_braces(lines, i)

            if not found:
                i += 1
                continue

            if end - start <= MAX_FUNCTION_LINES:
                results.append((start, end))
            i = end + 1
            continue

        # Case 2: 여러 줄 시그니처
        if "(" in stripped and ")" not in stripped:
            if stripped.endswith(";") or stripped.startswith("#"):
                i += 1
                continue

            cleaned_first = strip_strings_and_comments(stripped)
            paren_depth = cleaned_first.count("(") - cleaned_first.count(")")

            if paren_depth <= 0:
                i += 1
                continue

            found_close = False
            close_line = i

            for j in range(i + 1, min(i + 20, len(lines))):
                cl = strip_strings_and_comments(lines[j])
                paren_depth += cl.count("(") - cl.count(")")
                if paren_depth <= 0:
                    found_close = True
                    close_line = j
                    break

            if not found_close:
                i += 1
                continue

            found_body = False
            body_line = close_line
            for k in range(close_line, min(close_line + 5, len(lines))):
                if "{" in strip_strings_and_comments(lines[k]):
                    found_body = True
                    body_line = k
                    break

            if not found_body:
                i += 1
                continue

            found_brace, end = _track_braces(lines, body_line)

            if not found_brace:
                i += 1
                continue

            if end - start <= MAX_FUNCTION_LINES:
                results.append((start, end))
            i = end + 1
            continue

        i += 1

    return results


def _has_csharp_modifier_or_return_type(line):
    """줄의 첫 번째 단어가 접근 제한자, 수식어, 또는 반환 타입 힌트인지 확인."""
    words = line.split()
    if not words:
        return False

    first = words[0]
    base = first.split('<')[0].split('[')[0]

    if base in _CSHARP_MODIFIERS or base in _CSHARP_RETURN_HINTS:
        return True
    if base and base[0].isupper():
        return True

    return False


# [수정] Case 3 추가: Allman 스타일 — () 같은 줄, { 다음 줄
def extract_csharp_functions(lines):
    results = []
    i = 0

    while i < len(lines):
        line = lines[i].strip()

        if _is_keyword_start(line):
            i += 1
            continue

        # --- Case 1: 한 줄 시그니처 (K&R brace) ---
        if "(" in line and ")" in line and ("{" in line or "=>" in line):
            start = i

            if "=>" in line:
                results.append((i, i))
                i += 1
                continue

            found, end = _track_braces(lines, i)

            if not found:
                i += 1
                continue

            results.append((start, end))
            i = end + 1
            continue

        # --- Case 2: 여러 줄 시그니처 ---
        if "(" in line and ")" not in line:
            if not _has_csharp_modifier_or_return_type(line):
                i += 1
                continue

            start = i
            cleaned_first = strip_strings_and_comments(line)
            paren_depth = cleaned_first.count("(") - cleaned_first.count(")")

            if paren_depth <= 0:
                i += 1
                continue

            found_close = False
            close_line = i

            for j in range(i + 1, min(i + 20, len(lines))):
                cl = strip_strings_and_comments(lines[j])
                paren_depth += cl.count("(") - cl.count(")")
                if paren_depth <= 0:
                    found_close = True
                    close_line = j
                    break

            if not found_close:
                i += 1
                continue

            close_stripped = lines[close_line].strip()
            if _is_keyword_start(close_stripped):
                i += 1
                continue

            found_body = False
            body_line = close_line
            for k in range(close_line, min(close_line + 5, len(lines))):
                bl = lines[k].strip()
                if "{" in bl or "=>" in bl:
                    found_body = True
                    body_line = k
                    break

            if not found_body:
                i += 1
                continue

            if "=>" in lines[body_line].strip():
                results.append((start, body_line))
                i = body_line + 1
                continue

            found_brace, end = _track_braces(lines, body_line)

            if not found_brace:
                i += 1
                continue

            results.append((start, end))
            i = end + 1
            continue

        # --- [추가] Case 3: Allman 스타일 — () 같은 줄, { 다음 줄 ---
        if "(" in line and ")" in line and "{" not in line and "=>" not in line:
            # [추가] 세미콜론으로 끝나면 선언/호출 → 스킵
            if line.endswith(";"):
                i += 1
                continue

            # [추가] 다음 줄에서 { 탐색 (최대 3줄)
            found_brace_line = False
            brace_line = i
            for k in range(i + 1, min(i + 4, len(lines))):
                bl = lines[k].strip()
                if bl == "{":  # [추가] 단독 { 줄만 매칭 (오탐 최소화)
                    found_brace_line = True
                    brace_line = k
                    break
                if bl and not bl.startswith("//") and bl != "":
                    break  # [추가] 비공백/비주석 줄이 먼저 오면 중단

            if not found_brace_line:
                i += 1
                continue

            start = i
            found, end = _track_braces(lines, brace_line)

            if not found:
                i += 1
                continue

            results.append((start, end))
            i = end + 1
            continue

        i += 1

    return results


# [추가] Python def/async def 통합 판별 헬퍼
def _is_python_def(stripped):
    """'def ' 또는 'async def '로 시작하는지 확인."""
    return stripped.startswith("def ") or stripped.startswith("async def ")


# [수정] async def 지원 추가
def extract_python_functions(lines):
    results = []
    prev_end = -1

    for i, line in enumerate(lines):
        stripped = line.strip()
        if _is_python_def(stripped):  # [수정] async def 포함
            indent = len(line) - len(line.lstrip())
            start = i
            end = i + 1

            while end < len(lines):
                l = lines[end]
                if l.strip() == "":
                    end += 1
                    continue
                current_indent = len(l) - len(l.lstrip())
                if current_indent <= indent:
                    break
                end += 1

            actual_end = end - 1
            while actual_end > start and lines[actual_end].strip() == "":
                actual_end -= 1

            dec_start = start
            while dec_start > 0 and lines[dec_start - 1].strip().startswith("@"):
                dec_start -= 1

            if dec_start <= prev_end:
                dec_start = start

            results.append((dec_start, actual_end))
            prev_end = actual_end

    return results


def extract_functions(file_path, content):
    lines = content.splitlines()

    if file_path.endswith((".cpp", ".cc", ".h", ".hpp")):
        return extract_cpp_functions(lines)
    if file_path.endswith(".cs"):
        return extract_csharp_functions(lines)
    if file_path.endswith(".py"):
        return extract_python_functions(lines)

    return []


def extract_existing_comment(file_path, lines, start):
    if file_path.endswith(".py"):
        return _extract_python_docstring(lines, start)
    return _extract_above_comment(lines, start)


def _extract_python_docstring(lines, start):
    def_line = start
    for j in range(start, min(start + 10, len(lines))):
        if _is_python_def(lines[j].strip()):  # [수정] async def 포함
            def_line = j
            break

    for j in range(def_line + 1, min(def_line + 5, len(lines))):
        stripped = lines[j].strip()
        if stripped == "":
            continue

        for quote in ('"""', "'''"):
            if not stripped.startswith(quote):
                continue

            content_after_open = stripped[3:]

            if content_after_open == quote:
                return ""

            if content_after_open == "":
                return _read_multiline_docstring(lines, j + 1, quote)

            close_pos = content_after_open.rfind(quote)
            if close_pos > 0:
                return content_after_open[:close_pos].strip()

            return _read_multiline_docstring(lines, j + 1, quote, [content_after_open])

        break

    return ""


def _read_multiline_docstring(lines, from_line, quote, initial_lines=None):
    doc_lines = list(initial_lines) if initial_lines else []

    for k in range(from_line, min(from_line + 50, len(lines))):
        line_stripped = lines[k].strip()
        if line_stripped == quote:
            return "\n".join(part for part in doc_lines if part).strip()
        if line_stripped.endswith(quote):
            doc_lines.append(line_stripped[:-len(quote)].strip())
            return "\n".join(part for part in doc_lines if part).strip()
        doc_lines.append(line_stripped)

    return "\n".join(doc_lines).strip()


def _extract_above_comment(lines, start):
    result = []
    i = start - 1
    consecutive_blanks = 0
    found_line_comment = False
    in_block_comment = False

    while i >= 0:
        line = lines[i].strip()

        if line == "":
            consecutive_blanks += 1
            if in_block_comment:
                result.append("")
                i -= 1
                continue
            if consecutive_blanks > MAX_CONSECUTIVE_BLANK_LINES:
                break
            if found_line_comment:
                break
            i -= 1
            continue

        consecutive_blanks = 0

        if in_block_comment:
            result.append(line)
            if line.startswith("/*"):
                in_block_comment = False
            i -= 1
            continue

        if line.endswith("*/"):
            result.append(line)
            if line.startswith("/*"):
                pass
            else:
                in_block_comment = True
            i -= 1
            continue

        if (
            "@brief" in line or
            line.startswith("///") or
            line.startswith("/**") or
            line.startswith("/*") or
            (line.startswith("*") and not line.startswith("*/")
             and len(line) > 1 and line[1] in (' ', '\t', '/'))
        ):
            result.append(line)
            found_line_comment = True
            i -= 1
            continue

        break

    return "\n".join(reversed(result))


def extract_function_name(file_path, lines, start):
    search_start = start
    if file_path.endswith(".py"):
        for j in range(start, min(start + 10, len(lines))):
            if _is_python_def(lines[j].strip()):  # [수정] async def 포함
                search_start = j
                break

    for i in range(search_start, min(search_start + 20, len(lines))):
        m = _RE_FUNC_NAME.search(lines[i])
        if m:
            return m.group(1).split("::")[-1]
    return None


def find_comment_line(file_path, lines, start):
    search_start = start
    if file_path.endswith(".py"):
        for j in range(start, min(start + 10, len(lines))):
            if _is_python_def(lines[j].strip()):  # [수정] async def 포함
                search_start = j
                break

    paren_depth = 0
    found_open = False
    for i in range(search_start, min(search_start + 20, len(lines))):
        cleaned = strip_strings_and_comments(lines[i])
        for ch in cleaned:
            if ch == '(':
                paren_depth += 1
                found_open = True
            elif ch == ')':
                paren_depth -= 1
        if found_open and paren_depth <= 0:
            return i + 1
    return start + 1


def extract_json(text):
    cleaned = _RE_CODE_FENCE.sub('', text)
    cleaned = cleaned.replace('```', '')

    start = cleaned.find("[")
    end = cleaned.rfind("]") + 1
    if start == -1 or end == 0:
        return None

    candidate = cleaned[start:end]
    try:
        json.loads(candidate)
        return candidate
    except json.JSONDecodeError:
        return None


def validate_results(parsed, valid_ids):
    validated = []
    seen_ids = set()

    for item in parsed:
        if not isinstance(item, dict):
            log.warning("Skipping non-dict result: %s", type(item))
            continue

        item_id = item.get("id")
        if item_id not in valid_ids:
            log.warning("Skipping unknown id: %s", item_id)
            continue

        if item_id in seen_ids:
            log.warning("Skipping duplicate id: %s", item_id)
            continue
        seen_ids.add(item_id)

        status = item.get("status")
        if status not in VALID_STATUSES:
            log.warning("Skipping invalid status '%s' (id=%s)", status, item_id)
            continue

        if status != "ok" and not item.get("comment"):
            log.warning("Skipping empty comment (id=%s)", item_id)
            continue

        validated.append(item)
    return validated


def build_batch_prompt(batch):
    return f"""
다음 JSON 배열의 각 함수에 대해 주석을 작성하세요.

출력은 반드시 JSON 배열로만 하세요.

입력:
{json.dumps(batch, ensure_ascii=False)}

출력 형식:
[
  {{
    "id": number,
    "status": "ok" | "new" | "improve",
    "comment": "...",
    "reason": "..."
  }}
]

규칙:
- 기존 주석이 충분하면 status = ok
- 없으면 new
- 부족하면 improve + reason
- 언어별 스타일 맞출 것 (cpp / csharp / python)
- 코드 기반 설명만 작성

반드시 포함:
- 상태 변화
- 실패 조건
- side effect

금지:
- 함수 이름 반복
- 모호한 표현 ("처리합니다", "수행합니다")
"""


def chunked_by_size(tasks):
    batch = []
    size = 0

    for t in tasks:
        code_len = len(t["code"]) * 2
        if batch and (len(batch) >= BATCH_SIZE or size + code_len > MAX_BATCH_CHARS):
            yield batch
            batch = []
            size = 0
        batch.append(t)
        size += code_len

    if batch:
        yield batch


def get_all_comments():
    comments = []
    page = 1
    while True:
        url = (f"https://api.github.com/repos/{repo}/pulls/{pr_number}/comments"
               f"?per_page=100&page={page}")
        r = safe_request(requests.get, url, headers=gh_headers(), timeout=REQUEST_TIMEOUT)
        if not r:
            break
        page_data = r.json()
        if not page_data:
            break
        comments.extend(page_data)
        page += 1
    return comments


def build_existing_map(comments):
    result = {}
    for c in comments:
        if MARKER not in c.get("body", ""):
            continue
        key = (c["path"], c.get("line"))
        result.setdefault(key, []).append(c)
    return result


def set_status(state, desc, commit_sha):
    safe_request(
        requests.post,
        f"https://api.github.com/repos/{repo}/statuses/{commit_sha}",
        headers=gh_headers(),
        json={
            "state": state,
            "context": "ai-review/gemini",
            "description": desc
        },
        timeout=REQUEST_TIMEOUT
    )


def main():
    commit_sha = resolve_commit_sha()

    try:
        with open("diff.txt", "r", encoding="utf-8", errors="ignore") as f:
            diff = f.read()
    except FileNotFoundError:
        log.error("diff.txt not found")
        set_status("failure", "diff.txt not found", commit_sha)
        return

    additions = parse_added_lines(diff)

    added_map = {}
    for a in additions:
        added_map.setdefault(a["file"], set()).add(a["line"])

    file_set = set(added_map.keys())

    tasks = []
    task_id = 1

    for file_path in file_set:
        content = get_file_content(file_path, commit_sha)
        if not content:
            continue

        lines = content.splitlines()
        funcs = extract_functions(file_path, content)

        for start, end in funcs:
            if not any((start + 1) <= l <= (end + 1) for l in added_map[file_path]):
                continue

            code = "\n".join(lines[start:end+1])
            existing = extract_existing_comment(file_path, lines, start)
            name = extract_function_name(file_path, lines, start)

            lang = "cpp"
            if file_path.endswith(".cs"):
                lang = "csharp"
            elif file_path.endswith(".py"):
                lang = "python"

            comment_line = find_comment_line(file_path, lines, start)

            tasks.append({
                "id": task_id,
                "file": file_path,
                "line": comment_line,
                "language": lang,
                "name": name,
                "code": code,
                "existing_comment": existing
            })
            task_id += 1

    if not tasks:
        log.info("No functions to review")
        set_status("success", "No functions to review", commit_sha)
        return

    valid_ids = {t["id"] for t in tasks}

    # --- Gemini API ---

    client = genai.Client(api_key=api_key)
    results = []
    fail_count = 0

    try:
        for batch_tasks in chunked_by_size(tasks):
            prompt = build_batch_prompt(batch_tasks)

            response = None
            for attempt in range(GEMINI_MAX_RETRIES):
                try:
                    response = client.models.generate_content(
                        model="gemini-2.5-flash",
                        contents=prompt,
                        config={
                            "response_mime_type": "application/json"
                        }
                    )
                    if response and response.text:
                        break
                except Exception as e:
                    log.warning("Gemini API attempt %d failed: %s", attempt + 1, e)
                    response = None
                    continue

            if not response or not response.text:
                fail_count += 1
                continue

            json_text = extract_json(response.text)
            if not json_text:
                fail_count += 1
                continue

            try:
                parsed = json.loads(json_text)
                validated = validate_results(parsed, valid_ids)
                results.extend(validated)
            except json.JSONDecodeError as e:
                log.warning("JSON parse failed: %s", e)
                fail_count += 1
                continue
    finally:
        client.close()

    # --- GitHub 코멘트 관리 ---

    existing_comments = get_all_comments()
    existing_map = build_existing_map(existing_comments)

    task_map = {t["id"]: t for t in tasks}
    current_keys = set()
    stats = {"created": 0, "updated": 0, "deleted": 0, "skipped_ok": 0}

    for result in results:
        if result.get("status") == "ok":
            stats["skipped_ok"] += 1
            continue

        t = task_map.get(result.get("id"))
        if not t:
            continue

        key = (t["file"], t["line"])
        current_keys.add(key)

        fence = FENCE_MAP.get(t["language"], "text")

        if t["language"] == "python":
            comment_block = f'"""\n{result.get("comment","")}\n"""'
        else:
            comment_block = result.get("comment", "")

        if result.get("status") == "improve":
            body = f"""{MARKER}
### 💬 Suggested Improvement

```{fence}
{comment_block}
```

📌 이유:
{result.get("reason","")}
"""
        else:
            body = f"""{MARKER}
### ✨ Suggested Comment

```{fence}
{comment_block}
```
"""

        if key in existing_map:
            comment_list = existing_map[key]
            safe_request(
                requests.patch,
                f"https://api.github.com/repos/{repo}/pulls/comments/{comment_list[0]['id']}",
                headers=gh_headers(),
                json={"body": body},
                timeout=REQUEST_TIMEOUT
            )
            stats["updated"] += 1
            for dup in comment_list[1:]:
                safe_request(
                    requests.delete,
                    f"https://api.github.com/repos/{repo}/pulls/comments/{dup['id']}",
                    headers=gh_headers(),
                    timeout=REQUEST_TIMEOUT
                )
                stats["deleted"] += 1
        else:
            safe_request(
                requests.post,
                f"https://api.github.com/repos/{repo}/pulls/{pr_number}/comments",
                headers=gh_headers(),
                json={
                    "body": body,
                    "commit_id": commit_sha,
                    "path": t["file"],
                    "line": t["line"],
                    "side": "RIGHT"
                },
                timeout=REQUEST_TIMEOUT
            )
            stats["created"] += 1

    for key, comment_list in existing_map.items():
        if key not in current_keys:
            for c in comment_list:
                safe_request(
                    requests.delete,
                    f"https://api.github.com/repos/{repo}/pulls/comments/{c['id']}",
                    headers=gh_headers(),
                    timeout=REQUEST_TIMEOUT
                )
                stats["deleted"] += 1

    log.info("")
    log.info("=" * 50)
    log.info("Files scanned     : %d", len(file_set))
    log.info("Functions detected : %d", len(tasks))
    log.info("Gemini batches    : failed %d", fail_count)
    log.info("Comments created  : %d", stats['created'])
    log.info("Comments updated  : %d", stats['updated'])
    log.info("Comments deleted  : %d", stats['deleted'])
    log.info("Skipped (ok)      : %d", stats['skipped_ok'])
    log.info("=" * 50)

    if fail_count > 0:
        set_status("failure", "AI processing partially failed", commit_sha)
    else:
        set_status("success", "AI comment generation completed", commit_sha)


if __name__ == "__main__":
    main()
