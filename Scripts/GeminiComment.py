import os
import re
import json
import requests
import base64
from google import genai

MAX_FUNCTION_LINES = 500
REQUEST_TIMEOUT = 15
BATCH_SIZE = 20
MAX_BATCH_CHARS = 20000

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

def safe_request(method, url, **kwargs):
    for _ in range(3):
        r = method(url, **kwargs)
        if r and r.status_code in (200, 201):
            return r
    return None

_file_cache = {}

def get_file_content(path):
    if path in _file_cache:
        return _file_cache[path]

    url = f"https://api.github.com/repos/{repo}/contents/{path}?ref={commit_sha}"
    r = safe_request(requests.get, url, headers=gh_headers(), timeout=REQUEST_TIMEOUT)

    if not r:
        _file_cache[path] = None
        return None

    data = r.json()
    if "content" not in data:
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
            m = re.search(r"\+(\d+)", line)
            if m:
                new_line_num = int(m.group(1)) - 1
        elif line.startswith("+") and not line.startswith("+++"):
            new_line_num += 1
            if current_file:
                results.append({"file": current_file, "line": new_line_num})
        elif not line.startswith("-"):
            new_line_num += 1

    return results

def is_cpp_signature(line):
    s = line.strip()
    if not ("(" in s and ")" in s):
        return False
    if s.startswith(("if", "for", "while", "switch", "catch")):
        return False
    if any(x in s for x in ["=", "return", "sizeof"]):
        return False
    return True

def extract_cpp_functions(lines):
    results = []
    i = 0
    while i < len(lines):
        if not is_cpp_signature(lines[i]):
            i += 1
            continue

        start = i
        brace = 0
        found = False
        end = i

        while end < len(lines):
            brace += lines[end].count("{")
            brace -= lines[end].count("}")

            if "{" in lines[end]:
                found = True

            if found and brace == 0:
                break

            end += 1

        if end - start <= MAX_FUNCTION_LINES:
            results.append((start, end))

        i = end + 1

    return results

def extract_csharp_functions(lines):
    results = []
    i = 0

    while i < len(lines):
        line = lines[i].strip()

        if "(" in line and ")" in line and ("{" in line or "=>" in line):
            start = i

            if "=>" in line:
                results.append((i, i))
                i += 1
                continue

            brace = 0
            found = False
            end = i

            while end < len(lines):
                brace += lines[end].count("{")
                brace -= lines[end].count("}")

                if "{" in lines[end]:
                    found = True

                if found and brace == 0:
                    break

                end += 1

            results.append((start, end))
            i = end + 1
            continue

        i += 1

    return results

def extract_python_functions(lines):
    results = []

    for i, line in enumerate(lines):
        if line.strip().startswith("def "):
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

            results.append((start, end))

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

def extract_existing_comment(lines, start):
    result = []
    i = start - 1

    while i >= 0:
        line = lines[i].strip()

        if line == "":
            i -= 1
            continue

        if (
            "@brief" in line or
            line.startswith("///") or
            line.startswith("*") or
            line.startswith("/**")
        ):
            result.append(line)
            i -= 1
            continue

        break

    return "\n".join(reversed(result))

def extract_function_name(line):
    m = re.search(r'([~\w:<>]+)\s*\(', line)
    if not m:
        return None
    return m.group(1).split("::")[-1]

def find_comment_line(lines, start):
    for i in range(start, min(start + 5, len(lines))):
        if "(" in lines[i] and ")" in lines[i]:
            return i + 1
    return start + 1

def extract_json(text):
    start = text.find("[")
    end = text.rfind("]") + 1
    if start == -1 or end == 0:
        return None
    return text[start:end]

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

with open("diff.txt", "r", encoding="utf-8", errors="ignore") as f:
    diff = f.read()

additions = parse_added_lines(diff)

added_map = {}
for a in additions:
    added_map.setdefault(a["file"], set()).add(a["line"])

file_set = set(added_map.keys())

tasks = []
task_id = 1

for file_path in file_set:
    content = get_file_content(file_path)
    if not content:
        continue

    lines = content.splitlines()
    funcs = extract_functions(file_path, content)

    for start, end in funcs:
        if not any((start + 1) <= l <= (end + 1) for l in added_map[file_path]):
            continue

        code = "\n".join(lines[start:end+1])
        existing = extract_existing_comment(lines, start)
        name = extract_function_name(lines[start])

        lang = "cpp"
        if file_path.endswith(".cs"):
            lang = "csharp"
        elif file_path.endswith(".py"):
            lang = "python"

        comment_line = find_comment_line(lines, start)

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

client = genai.Client(api_key=api_key)

results = []
fail_count = 0

for batch in chunked_by_size(tasks):
    prompt = build_batch_prompt(batch)

    response = client.models.generate_content(
        model="gemini-2.5-flash",
        contents=prompt
    )

    if not response or not response.text:
        fail_count += 1
        continue

    json_text = extract_json(response.text)
    if not json_text:
        fail_count += 1
        continue

    try:
        parsed = json.loads(json_text)
        results.extend(parsed)
    except:
        fail_count += 1
        continue

client.close()

MARKER = "<!-- GEMINI_FUNCTION_SUMMARY -->"

def get_all_comments():
    url = f"https://api.github.com/repos/{repo}/pulls/{pr_number}/comments"
    r = safe_request(requests.get, url, headers=gh_headers(), timeout=REQUEST_TIMEOUT)
    if not r:
        return []
    return r.json()

existing_comments = get_all_comments()

existing_map = {}
for c in existing_comments:
    if MARKER not in c.get("body", ""):
        continue
    key = (c["path"], c.get("line"))
    existing_map[key] = c

task_map = {t["id"]: t for t in tasks}
current_keys = set()

for r in results:
    if r.get("status") == "ok":
        continue

    t = task_map.get(r.get("id"))
    if not t:
        continue

    key = (t["file"], t["line"])
    current_keys.add(key)

    fence_map = {
        "cpp": "cpp",
        "csharp": "csharp",
        "python": "python"
    }

    fence = fence_map.get(t["language"], "text")

    if t["language"] == "python":
        comment_block = f'"""\n{r.get("comment","")}\n"""'
    else:
        comment_block = r.get("comment","")

    if r.get("status") == "improve":
        body = f"""{MARKER}
### 💬 Suggested Improvement

```{fence}
{comment_block}
```

📌 이유:
{r.get("reason","")}
"""
    else:
        body = f"""{MARKER}
### ✨ Suggested Comment

```{fence}
{comment_block}
```
"""

    if key in existing_map:
        safe_request(
            requests.patch,
            f"https://api.github.com/repos/{repo}/pulls/comments/{existing_map[key]['id']}",
            headers=gh_headers(),
            json={"body": body},
            timeout=REQUEST_TIMEOUT
        )
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

for key, c in existing_map.items():
    if key not in current_keys:
        safe_request(
            requests.delete,
            f"https://api.github.com/repos/{repo}/pulls/comments/{c['id']}",
            headers=gh_headers(),
            timeout=REQUEST_TIMEOUT
        )

def set_status(state, desc):
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

if fail_count > 0:
    set_status("failure", "AI processing partially failed")
else:
    set_status("success", "AI comment generation completed")
