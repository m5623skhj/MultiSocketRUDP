"""
변경 감지 모듈.
PR diff에서 변경된 파일과 함수를 식별하고 변경 유형을 분류한다.
범위 축소가 목적이므로 경량 파싱(정규식)으로 처리한다.
기존 GeminiComment.py의 parse_added_lines() 패턴을 참고.
"""

import re
import logging
from enum import Enum
from dataclasses import dataclass, field

from config import (
    CPP_HEADER_EXTENSIONS,
    CPP_SOURCE_EXTENSIONS,
    CSHARP_EXTENSIONS,
    TARGET_EXTENSIONS,
)

logger = logging.getLogger(__name__)


# ============================================================
# 변경 유형
# ============================================================
class ChangeType(Enum):
    ADDED = "added"
    MODIFIED = "modified"
    DELETED = "deleted"


# ============================================================
# 변경 함수 정보
# ============================================================
@dataclass
class ChangedFunction:
    """변경이 감지된 함수 정보."""
    file_path: str           # 변경된 파일 경로
    function_name: str       # 함수/메서드 이름
    class_name: str = ""     # 소속 클래스 이름
    change_type: ChangeType = ChangeType.MODIFIED
    is_header: bool = False  # C++ 헤더 파일 여부


@dataclass
class DiffFileInfo:
    """diff에서 추출한 파일별 변경 정보."""
    file_path: str
    status: str = ""                  # added / modified / removed
    added_lines: list[int] = field(default_factory=list)
    removed_lines: list[int] = field(default_factory=list)


# ============================================================
# diff 파싱
# ============================================================
def parse_diff(diff_text: str) -> list[DiffFileInfo]:
    """
    unified diff 텍스트를 파싱하여 파일별 변경 라인 정보를 추출한다.

    Args:
        diff_text: PR의 unified diff 텍스트.

    Returns:
        DiffFileInfo 리스트.
    """
    files: list[DiffFileInfo] = []
    current_file: DiffFileInfo | None = None
    new_line_num = 0
    old_line_num = 0

    for line in diff_text.splitlines():
        # 새 파일 시작 감지
        if line.startswith("diff --git"):
            # a/path b/path 에서 b/path 추출
            match = re.search(r"b/(.+)$", line)
            if match:
                file_path = match.group(1)
                current_file = DiffFileInfo(file_path=file_path)
                files.append(current_file)
            continue

        if not current_file:
            continue

        # 파일 상태 감지
        if line.startswith("new file"):
            current_file.status = "added"
        elif line.startswith("deleted file"):
            current_file.status = "removed"
        elif line.startswith("--- ") or line.startswith("+++ "):
            if not current_file.status:
                current_file.status = "modified"
            continue

        # hunk 헤더
        if line.startswith("@@ "):
            match = re.search(r"-(\d+)(?:,\d+)?\s+\+(\d+)", line)
            if match:
                old_line_num = int(match.group(1)) - 1
                new_line_num = int(match.group(2)) - 1
            continue

        # 추가된 라인
        if line.startswith("+") and not line.startswith("+++"):
            new_line_num += 1
            current_file.added_lines.append(new_line_num)
        # 삭제된 라인
        elif line.startswith("-") and not line.startswith("---"):
            old_line_num += 1
            current_file.removed_lines.append(old_line_num)
        # 컨텍스트 라인
        else:
            new_line_num += 1
            old_line_num += 1

    return files


# ============================================================
# 함수 시그니처 감지 (경량 파싱)
# ============================================================

# C++ 함수 시그니처 패턴: 반환타입 (클래스::)함수명(파라미터)
_CPP_FUNC_PATTERN = re.compile(
    r"""
    (?:^|\s)                          # 라인 시작 또는 공백
    (?:virtual\s+|static\s+|inline\s+|explicit\s+|constexpr\s+|\[\[nodiscard\]\]\s*)*  # 키워드
    [\w:<>&*\s]+?                     # 반환 타입
    \s+(~?\w+)                        # 함수 이름 (캡처)
    \s*\(                             # 여는 괄호
    """,
    re.VERBOSE,
)

# C# 메서드 시그니처 패턴
_CSHARP_METHOD_PATTERN = re.compile(
    r"""
    (?:public|private|protected|internal)\s+  # 접근 제한자
    (?:static\s+|virtual\s+|override\s+|abstract\s+|async\s+|sealed\s+)*  # 키워드
    [\w<>\[\]?,\s]+?                          # 반환 타입
    \s+(\w+)                                  # 메서드 이름 (캡처)
    \s*[(<]                                   # 여는 괄호 또는 제네릭
    """,
    re.VERBOSE,
)

# C++ 제어문 (함수로 오탐하지 않기 위한 필터)
_CPP_CONTROL_KEYWORDS = {"if", "for", "while", "switch", "catch", "return", "sizeof", "else"}


def _extract_cpp_function_name(line: str) -> str | None:
    """C++ 소스 라인에서 함수명을 추출한다. 제어문, 생성자, 소멸자는 제외."""
    stripped = line.strip()

    # 빈 라인, 전처리기, 주석 제외
    if not stripped or stripped.startswith(("#", "//", "/*", "*")):
        return None

    # 괄호가 없으면 함수가 아님
    if "(" not in stripped or ")" not in stripped:
        return None

    # 제어문 필터
    first_word = stripped.split("(")[0].strip().split()[-1] if stripped.split("(")[0].strip() else ""
    if first_word.lstrip("~") in _CPP_CONTROL_KEYWORDS:
        return None

    # 대입문, return문 필터
    if "=" in stripped.split("(")[0] or stripped.startswith("return"):
        return None

    # 추가: explicit 생성자 필터 (예: explicit RUDPSession(Core& core))
    if stripped.lstrip().startswith("explicit "):
        return None

    match = _CPP_FUNC_PATTERN.search(stripped)
    if match:
        name = match.group(1)
        # Class::Method 형태 처리
        if "::" in stripped:
            scope_match = re.search(r"(\w+)::(\w+)\s*\(", stripped)
            if scope_match:
                return scope_match.group(2)

        # 추가: 소멸자 필터 (~ 접두사)
        if name.startswith("~"):
            return None

        # 추가: 반환 타입이 없는 함수는 생성자일 가능성 높음 — 이름 앞에 타입이 없으면 제외
        before_name = stripped.split(name)[0].strip()
        # virtual, friend 등 키워드만 있고 타입이 없으면 생성자
        before_tokens = before_name.replace("[[nodiscard]]", "").strip()
        type_keywords = {"virtual", "static", "inline", "constexpr", "friend"}
        remaining = before_tokens
        for kw in type_keywords:
            remaining = remaining.replace(kw, "").strip()
        if not remaining:  # 수정: 타입 토큰이 없으면 생성자로 판단하여 제외
            return None

        return name

    return None


def _extract_csharp_method_name(line: str) -> str | None:
    """C# 소스 라인에서 메서드명을 추출한다."""
    stripped = line.strip()

    if not stripped or stripped.startswith("//"):
        return None

    match = _CSHARP_METHOD_PATTERN.search(stripped)
    if match:
        return match.group(1)

    return None


def _extract_class_name_from_path(file_path: str) -> str:
    """파일 경로에서 클래스명을 추출한다 (파일명 기반)."""
    import os
    basename = os.path.basename(file_path)
    name, _ = os.path.splitext(basename)
    return name


# ============================================================
# 변경된 함수 식별
# ============================================================
def identify_changed_functions(
    diff_files: list[DiffFileInfo],
    file_contents: dict[str, str | None],
) -> list[ChangedFunction]:
    """
    diff 정보와 파일 내용을 기반으로 변경된 함수 목록을 식별한다.

    Args:
        diff_files: parse_diff()의 결과.
        file_contents: {파일 경로: 파일 내용} 딕셔너리. (main 브랜치의 최종 코드)

    Returns:
        ChangedFunction 리스트.
    """
    changed_functions: list[ChangedFunction] = []
    seen = set()  # 중복 방지: (file_path, function_name)

    for diff_file in diff_files:
        file_path = diff_file.file_path

        # 대상 확장자가 아니면 스킵
        if not any(file_path.endswith(ext) for ext in TARGET_EXTENSIONS):
            continue

        is_cpp_header = any(file_path.endswith(ext) for ext in CPP_HEADER_EXTENSIONS)
        is_cpp_source = any(file_path.endswith(ext) for ext in CPP_SOURCE_EXTENSIONS)
        is_csharp = any(file_path.endswith(ext) for ext in CSHARP_EXTENSIONS)

        class_name = _extract_class_name_from_path(file_path)

        # 파일 전체가 추가된 경우
        if diff_file.status == "added":
            functions = _extract_all_functions_from_content(
                file_path, file_contents.get(file_path, "")
            )
            for func_name in functions:
                key = (file_path, func_name)
                if key not in seen:
                    seen.add(key)
                    changed_functions.append(ChangedFunction(
                        file_path=file_path,
                        function_name=func_name,
                        class_name=class_name,
                        change_type=ChangeType.ADDED,
                        is_header=is_cpp_header,
                    ))
            continue

        # 파일 전체가 삭제된 경우
        if diff_file.status == "removed":
            # 삭제된 파일의 내용은 이미 없으므로 diff의 삭제 라인에서 추출
            # (file_contents에는 없을 수 있음)
            changed_functions.append(ChangedFunction(
                file_path=file_path,
                function_name="*",  # 파일 전체 삭제 표시
                class_name=class_name,
                change_type=ChangeType.DELETED,
                is_header=is_cpp_header,
            ))
            continue

        # 수정된 파일: 변경된 라인이 포함된 함수 식별
        content = file_contents.get(file_path)
        if not content:
            continue

        lines = content.splitlines()
        changed_line_set = set(diff_file.added_lines + diff_file.removed_lines)

        # 함수 범위 매핑: 각 라인이 어떤 함수에 속하는지 파악
        func_ranges = _build_function_ranges(file_path, lines)

        for func_name, start, end in func_ranges:  # 수정: dict.items() → tuple unpacking
            # 변경된 라인이 이 함수 범위에 포함되는지 확인
            if any(start <= ln <= end for ln in changed_line_set):
                key = (file_path, func_name)
                if key not in seen:
                    seen.add(key)
                    changed_functions.append(ChangedFunction(
                        file_path=file_path,
                        function_name=func_name,
                        class_name=class_name,
                        change_type=ChangeType.MODIFIED,
                        is_header=is_cpp_header,
                    ))

    return changed_functions


def _extract_all_functions_from_content(
    file_path: str, content: str | None
) -> list[str]:
    """파일 내용에서 모든 함수명을 추출한다."""
    if not content:
        return []

    lines = content.splitlines()
    functions = []

    is_cpp = any(file_path.endswith(ext) for ext in CPP_HEADER_EXTENSIONS + CPP_SOURCE_EXTENSIONS)
    is_csharp = any(file_path.endswith(ext) for ext in CSHARP_EXTENSIONS)

    for line in lines:
        if is_cpp:
            name = _extract_cpp_function_name(line)
        elif is_csharp:
            name = _extract_csharp_method_name(line)
        else:
            continue

        if name and name not in functions:
            functions.append(name)

    return functions


def _build_function_ranges(
    file_path: str, lines: list[str]
) -> list[tuple[str, int, int]]:
    """
    파일 내 함수별 라인 범위를 구성한다.
    반환: [(함수명, 시작 라인, 끝 라인)] (1-based)
    수정: dict → list 변환. 동일 함수명 오버로드 시 덮어쓰기 방지.
    """
    ranges: list[tuple[str, int, int]] = []  # 수정: dict → list[tuple]

    is_cpp = any(file_path.endswith(ext) for ext in CPP_HEADER_EXTENSIONS + CPP_SOURCE_EXTENSIONS)
    is_csharp = any(file_path.endswith(ext) for ext in CSHARP_EXTENSIONS)
    is_header = any(file_path.endswith(ext) for ext in CPP_HEADER_EXTENSIONS)

    i = 0
    while i < len(lines):
        line = lines[i]

        if is_cpp:
            name = _extract_cpp_function_name(line)
        elif is_csharp:
            name = _extract_csharp_method_name(line)
        else:
            i += 1
            continue

        if not name:
            i += 1
            continue

        start = i + 1  # 1-based

        stripped = line.strip()
        if is_header and stripped.endswith(";") and "{" not in stripped:
            ranges.append((name, start, start))  # 수정: list.append
            i += 1
            continue

        brace_count = 0
        found_brace = False
        end = i

        while end < len(lines):
            brace_count += lines[end].count("{")
            brace_count -= lines[end].count("}")

            if "{" in lines[end]:
                found_brace = True

            if found_brace and brace_count == 0:
                break

            if not found_brace and end > i:
                next_stripped = lines[end].strip()
                if next_stripped.startswith(("public:", "private:", "protected:", "class ", "struct ")):
                    end = i
                    break

            end += 1

        end_line = min(end + 1, len(lines))  # 1-based
        ranges.append((name, start, end_line))  # 수정: list.append

        i = end + 1

    return ranges


# ============================================================
# 변경 분류: 내부 구현만 변경된 경우 필터링
# ============================================================
def filter_interface_changes(
    changes: list[ChangedFunction],
    diff_files: list[DiffFileInfo],
) -> list[ChangedFunction]:
    """
    인터페이스 변경만 남기고 내부 구현만 변경된 항목을 필터링한다.

    규칙:
    - C++ 헤더 파일 (.h/.hpp) 변경은 항상 포함 (인터페이스 변경)
    - C++ 소스 파일 (.cpp) 변경인데, 동일 함수의 헤더가 변경되지 않았으면 제외
    - C# 파일은 인터페이스와 구현이 같은 파일에 있으므로 항상 포함
    - ADDED/DELETED는 항상 포함

    Args:
        changes: identify_changed_functions()의 결과.
        diff_files: parse_diff()의 결과.

    Returns:
        필터링된 ChangedFunction 리스트.
    """
    # 변경된 헤더 파일의 함수명 수집
    header_changed_funcs = set()
    for change in changes:
        if change.is_header:
            header_changed_funcs.add(change.function_name)

    # 변경된 파일 경로 수집
    changed_file_paths = {df.file_path for df in diff_files}

    filtered = []
    for change in changes:
        # ADDED / DELETED는 항상 포함
        if change.change_type in (ChangeType.ADDED, ChangeType.DELETED):
            filtered.append(change)
            continue

        # C++ 헤더 변경은 항상 포함
        if change.is_header:
            filtered.append(change)
            continue

        # C# 파일은 항상 포함
        if any(change.file_path.endswith(ext) for ext in CSHARP_EXTENSIONS):
            filtered.append(change)
            continue

        # C++ 소스 파일: 해당 함수의 헤더도 변경되었으면 포함
        if change.function_name in header_changed_funcs:
            filtered.append(change)
            continue

        # C++ 소스만 변경 → 내부 구현 변경으로 판단, 제외
        logger.debug(
            "내부 구현만 변경으로 판단, 제외: %s::%s (소스만 변경, 헤더 미변경)",
            change.class_name, change.function_name,
        )

    logger.info(
        "인터페이스 변경 필터링: %d → %d개",
        len(changes), len(filtered),
    )
    return filtered


# ============================================================
# 메인 진입점
# ============================================================
def detect_changes(
    merged_prs: list,
    file_contents: dict[str, str | None],
) -> list[ChangedFunction]:
    """
    머지된 PR들의 diff를 분석하여 변경된 함수 목록을 반환한다.

    Args:
        merged_prs: git_utils.MergedPR 리스트.
        file_contents: {파일 경로: 파일 내용} 딕셔너리.

    Returns:
        필터링 완료된 ChangedFunction 리스트.
    """
    all_diff_files: list[DiffFileInfo] = []
    all_changes: list[ChangedFunction] = []

    for pr in merged_prs:
        if not pr.diff:
            logger.warning("PR #%d의 diff가 비어 있음", pr.number)
            continue

        diff_files = parse_diff(pr.diff)
        all_diff_files.extend(diff_files)

        changes = identify_changed_functions(diff_files, file_contents)
        all_changes.extend(changes)

        logger.info("PR #%d: %d개 변경 함수 감지", pr.number, len(changes))

    # 중복 제거 (같은 함수를 여러 PR이 수정한 경우)
    unique_changes = _deduplicate_changes(all_changes)

    # 내부 구현 변경 필터링
    filtered = filter_interface_changes(unique_changes, all_diff_files)

    return filtered


def _deduplicate_changes(changes: list[ChangedFunction]) -> list[ChangedFunction]:
    """동일 함수에 대한 중복 변경을 제거한다. 최종 코드 상태 기준이므로 하나만 남긴다."""
    seen = set()
    unique = []

    for change in changes:
        key = (change.file_path, change.function_name)
        if key not in seen:
            seen.add(key)
            unique.append(change)

    if len(changes) != len(unique):
        logger.info("중복 제거: %d → %d개", len(changes), len(unique))

    return unique
