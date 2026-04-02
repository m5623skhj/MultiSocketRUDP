"""
코드 추출기 모듈.
main 브랜치의 최종 코드에서 함수 시그니처, 파라미터, 반환 타입 등
구조화된 정보를 추출한다.
tree-sitter 기반 AST 파싱을 사용하며, tree-sitter 미설치 시 정규식 fallback을 제공한다.
"""

import re
import logging
from dataclasses import dataclass, field

from change_detector import ChangedFunction, ChangeType
from config import CPP_HEADER_EXTENSIONS, CPP_SOURCE_EXTENSIONS, CSHARP_EXTENSIONS

logger = logging.getLogger(__name__)


# ============================================================
# 추출 결과 데이터 클래스
# ============================================================
@dataclass
class FunctionInfo:
    """AI에 전달할 구조화된 함수 정보."""
    name: str
    class_name: str
    file_path: str
    change_type: str             # "added" / "modified" / "deleted"
    signature: str = ""          # 전체 시그니처 (한 줄)
    return_type: str = ""
    parameters: list[dict] = field(default_factory=list)  # [{"type": "...", "name": "..."}, ...]
    access_modifier: str = ""    # public / private / protected
    qualifiers: list[str] = field(default_factory=list)   # virtual, static, override, [[nodiscard]], ...
    doc_comment: str = ""        # 기존 @brief, @param 등 주석
    error_cases: list[str] = field(default_factory=list)  # throw, 에러 반환 관련 정보
    raw_code: str = ""           # 함수 선언부 원본 코드


# ============================================================
# tree-sitter 사용 여부 확인
# ============================================================
_TREE_SITTER_AVAILABLE = False
_ts_cpp_parser = None
_ts_csharp_parser = None

try:
    import tree_sitter_cpp as tscpp
    import tree_sitter_c_sharp as tscsharp
    from tree_sitter import Language, Parser

    _cpp_language = Language(tscpp.language())
    _csharp_language = Language(tscsharp.language())

    _ts_cpp_parser = Parser(_cpp_language)
    _ts_csharp_parser = Parser(_csharp_language)

    _TREE_SITTER_AVAILABLE = True
    logger.info("tree-sitter 사용 가능 (AST 기반 추출)")
except ImportError:
    logger.warning("tree-sitter 미설치 → 정규식 fallback 사용")


# ============================================================
# 메인 추출 함수
# ============================================================
def extract_function_info(
    changed: ChangedFunction,
    file_content: str | None,
) -> FunctionInfo | None:
    """
    변경된 함수에 대해 main 브랜치의 최종 코드에서 구조화된 정보를 추출한다.

    Args:
        changed: ChangedFunction 객체.
        file_content: 해당 파일의 최종 내용.

    Returns:
        FunctionInfo 객체. DELETED이거나 추출 실패 시 삭제 표시용 FunctionInfo 또는 None.
    """
    # DELETED는 코드가 없으므로 최소 정보만 반환
    if changed.change_type == ChangeType.DELETED:
        return FunctionInfo(
            name=changed.function_name,
            class_name=changed.class_name,
            file_path=changed.file_path,
            change_type="deleted",
        )

    if not file_content:
        logger.warning("파일 내용 없음: %s", changed.file_path)
        return None

    is_cpp = any(
        changed.file_path.endswith(ext)
        for ext in CPP_HEADER_EXTENSIONS + CPP_SOURCE_EXTENSIONS
    )
    is_csharp = any(changed.file_path.endswith(ext) for ext in CSHARP_EXTENSIONS)

    if _TREE_SITTER_AVAILABLE:
        if is_cpp:
            return _extract_cpp_tree_sitter(changed, file_content)
        elif is_csharp:
            return _extract_csharp_tree_sitter(changed, file_content)
    else:
        if is_cpp:
            return _extract_cpp_regex(changed, file_content)
        elif is_csharp:
            return _extract_csharp_regex(changed, file_content)

    return None


def extract_all(
    changes: list[ChangedFunction],
    file_contents: dict[str, str | None],
) -> list[FunctionInfo]:
    """
    변경된 함수 전체에 대해 코드 정보를 추출한다.

    Args:
        changes: ChangedFunction 리스트.
        file_contents: {파일 경로: 파일 내용} 딕셔너리.

    Returns:
        FunctionInfo 리스트.
    """
    results = []

    for changed in changes:
        content = file_contents.get(changed.file_path)
        info = extract_function_info(changed, content)

        if info:
            results.append(info)
            logger.debug("추출 완료: %s::%s (%s)", info.class_name, info.name, info.change_type)
        else:
            logger.warning("추출 실패: %s::%s", changed.class_name, changed.function_name)

    logger.info("코드 추출 완료: %d / %d", len(results), len(changes))
    return results


# ============================================================
# tree-sitter: C++ 추출
# ============================================================
def _extract_cpp_tree_sitter(changed: ChangedFunction, content: str) -> FunctionInfo | None:
    """tree-sitter를 사용하여 C++ 함수 정보를 추출한다."""
    tree = _ts_cpp_parser.parse(bytes(content, "utf-8"))
    root = tree.root_node

    target_name = changed.function_name

    # 함수 선언/정의 노드 탐색
    for node in _walk_tree(root):
        if node.type in ("function_definition", "function_declarator", "declaration"):
            func_name = _get_cpp_function_name(node)
            if func_name == target_name:
                return _build_cpp_function_info(changed, node, content)

    # class 내부 메서드 탐색
    for node in _walk_tree(root):
        if node.type == "class_specifier":
            for child in _walk_tree(node):
                if child.type in ("function_definition", "declaration"):
                    func_name = _get_cpp_function_name(child)
                    if func_name == target_name:
                        return _build_cpp_function_info(changed, child, content)

    logger.debug("tree-sitter: C++ 함수 '%s' 노드를 찾지 못함 → regex fallback", target_name)
    return _extract_cpp_regex(changed, content)


def _get_cpp_function_name(node) -> str | None:
    """AST 노드에서 C++ 함수명을 추출한다."""
    for child in _walk_tree(node):
        if child.type == "function_declarator":
            for sub in child.children:
                if sub.type in ("identifier", "field_identifier", "destructor_name",
                                "qualified_identifier"):
                    text = sub.text.decode("utf-8")
                    # Class::Method → Method
                    if "::" in text:
                        return text.split("::")[-1]
                    return text
        if child.type in ("identifier", "field_identifier") and node.type == "declaration":
            # 단순 선언에서의 이름
            return child.text.decode("utf-8")
    return None


def _build_cpp_function_info(changed: ChangedFunction, node, content: str) -> FunctionInfo:
    """AST 노드에서 C++ FunctionInfo를 구성한다."""
    lines = content.splitlines()
    start_line = node.start_point[0]
    end_line = node.end_point[0]

    # 함수 선언부 텍스트
    raw_code = "\n".join(lines[start_line:end_line + 1])

    # 시그니처 추출 (중괄호 이전까지)
    signature_lines = []
    for line in lines[start_line:end_line + 1]:
        if "{" in line:
            signature_lines.append(line.split("{")[0].strip())
            break
        signature_lines.append(line.strip())
    signature = " ".join(signature_lines).strip().rstrip(";")

    # 반환 타입 추출
    return_type = ""
    for child in node.children:
        if child.type in ("type_identifier", "primitive_type", "template_type", "auto"):
            return_type = child.text.decode("utf-8")
            break

    # 추가: tree-sitter에서 반환 타입을 못 찾으면 시그니처에서 추출 (fallback)
    if not return_type and signature:
        sig_before_name = signature.split(changed.function_name)[0].strip()
        # [[nodiscard]], virtual, static 등 한정자 제거 후 마지막 토큰이 반환 타입
        for keyword in ("virtual", "static", "inline", "explicit", "constexpr",
                        "[[nodiscard]]", "override"):
            sig_before_name = sig_before_name.replace(keyword, "").strip()
        if sig_before_name:
            return_type = sig_before_name.split()[-1] if sig_before_name.split() else ""  # 수정: fallback 추출

    # 파라미터 추출
    parameters = []
    for child in _walk_tree(node):
        if child.type == "parameter_list":
            for param in child.children:
                if param.type == "parameter_declaration":
                    param_text = param.text.decode("utf-8").strip()
                    parameters.append(_parse_cpp_param(param_text))
            break

    # 한정자 추출
    qualifiers = _extract_cpp_qualifiers(lines, start_line)

    # 주석 추출
    doc_comment = _extract_doc_comment(lines, start_line)

    # 접근 제한자 추출
    access_modifier = _detect_cpp_access(lines, start_line)

    return FunctionInfo(
        name=changed.function_name,
        class_name=changed.class_name,
        file_path=changed.file_path,
        change_type=changed.change_type.value,
        signature=signature,
        return_type=return_type,
        parameters=parameters,
        access_modifier=access_modifier,
        qualifiers=qualifiers,
        doc_comment=doc_comment,
        raw_code=raw_code[:2000],  # 과도한 코드 제한
    )


# ============================================================
# tree-sitter: C# 추출
# ============================================================
def _extract_csharp_tree_sitter(changed: ChangedFunction, content: str) -> FunctionInfo | None:
    """tree-sitter를 사용하여 C# 메서드 정보를 추출한다."""
    tree = _ts_csharp_parser.parse(bytes(content, "utf-8"))
    root = tree.root_node

    target_name = changed.function_name

    for node in _walk_tree(root):
        if node.type == "method_declaration":
            name_node = node.child_by_field_name("name")
            if name_node and name_node.text.decode("utf-8") == target_name:
                return _build_csharp_function_info(changed, node, content)

    logger.debug("tree-sitter: C# 메서드 '%s' 노드를 찾지 못함 → regex fallback", target_name)
    return _extract_csharp_regex(changed, content)


def _build_csharp_function_info(changed: ChangedFunction, node, content: str) -> FunctionInfo:
    """AST 노드에서 C# FunctionInfo를 구성한다."""
    lines = content.splitlines()
    start_line = node.start_point[0]
    end_line = node.end_point[0]

    raw_code = "\n".join(lines[start_line:end_line + 1])

    # 시그니처
    signature_lines = []
    for line in lines[start_line:end_line + 1]:
        if "{" in line:
            signature_lines.append(line.split("{")[0].strip())
            break
        signature_lines.append(line.strip())
    signature = " ".join(signature_lines).strip()

    # 반환 타입
    return_type = ""
    type_node = node.child_by_field_name("type")
    if type_node:
        return_type = type_node.text.decode("utf-8")

    # 파라미터
    parameters = []
    params_node = node.child_by_field_name("parameters")
    if params_node:
        for param in params_node.children:
            if param.type == "parameter":
                param_text = param.text.decode("utf-8").strip()
                parts = param_text.rsplit(" ", 1)
                if len(parts) == 2:
                    parameters.append({"type": parts[0], "name": parts[1]})

    # 한정자
    qualifiers = []
    for child in node.children:
        if child.type == "modifier":
            qualifiers.append(child.text.decode("utf-8"))

    # 접근 제한자
    access_modifier = ""
    for q in qualifiers:
        if q in ("public", "private", "protected", "internal"):
            access_modifier = q
            break

    # 주석
    doc_comment = _extract_doc_comment(lines, start_line)

    return FunctionInfo(
        name=changed.function_name,
        class_name=changed.class_name,
        file_path=changed.file_path,
        change_type=changed.change_type.value,
        signature=signature,
        return_type=return_type,
        parameters=parameters,
        access_modifier=access_modifier,
        qualifiers=qualifiers,
        doc_comment=doc_comment,
        raw_code=raw_code[:2000],
    )


# ============================================================
# 정규식 Fallback: C++
# ============================================================
def _extract_cpp_regex(changed: ChangedFunction, content: str) -> FunctionInfo | None:
    """정규식 기반으로 C++ 함수 정보를 추출한다. (tree-sitter 미설치 시 fallback)"""
    lines = content.splitlines()
    target = changed.function_name

    for i, line in enumerate(lines):
        # 함수명이 포함된 라인 탐색
        if target not in line:
            continue

        # 함수 시그니처 패턴 매칭
        if "(" in line and not line.strip().startswith(("//", "/*", "*", "#")):
            # Class::Method 또는 단순 Method 패턴
            pattern = rf"(?:\w+::)?{re.escape(target)}\s*\("
            if re.search(pattern, line):
                # 시그니처 수집 (중괄호 또는 세미콜론까지)
                sig_lines = []
                j = i
                while j < len(lines):
                    sig_lines.append(lines[j].strip())
                    if "{" in lines[j] or lines[j].strip().endswith(";"):
                        break
                    j += 1

                signature = " ".join(sig_lines).split("{")[0].strip().rstrip(";")

                # 파라미터 추출
                param_match = re.search(r"\(([^)]*)\)", signature)
                parameters = []
                if param_match:
                    params_str = param_match.group(1).strip()
                    if params_str:
                        for p in params_str.split(","):
                            p = p.strip()
                            parameters.append(_parse_cpp_param(p))

                # 반환 타입 추출
                before_name = signature.split(target)[0].strip()
                return_type = before_name.split()[-1] if before_name.split() else ""

                # 주석
                doc_comment = _extract_doc_comment(lines, i)

                # 한정자
                qualifiers = _extract_cpp_qualifiers(lines, i)

                return FunctionInfo(
                    name=target,
                    class_name=changed.class_name,
                    file_path=changed.file_path,
                    change_type=changed.change_type.value,
                    signature=signature,
                    return_type=return_type,
                    parameters=parameters,
                    access_modifier=_detect_cpp_access(lines, i),
                    qualifiers=qualifiers,
                    doc_comment=doc_comment,
                    raw_code="\n".join(lines[i:min(i + 30, len(lines))]),
                )

    return None


# ============================================================
# 정규식 Fallback: C#
# ============================================================
def _extract_csharp_regex(changed: ChangedFunction, content: str) -> FunctionInfo | None:
    """정규식 기반으로 C# 메서드 정보를 추출한다."""
    lines = content.splitlines()
    target = changed.function_name

    pattern = re.compile(
        rf"((?:public|private|protected|internal)\s+"
        rf"(?:static\s+|virtual\s+|override\s+|async\s+|sealed\s+)*"
        rf"[\w<>\[\]?,\s]+?\s+{re.escape(target)}\s*[(<])"
    )

    for i, line in enumerate(lines):
        if pattern.search(line):
            sig_lines = []
            j = i
            while j < len(lines):
                sig_lines.append(lines[j].strip())
                if "{" in lines[j] or "=>" in lines[j]:
                    break
                j += 1

            signature = " ".join(sig_lines).split("{")[0].split("=>")[0].strip()
            doc_comment = _extract_doc_comment(lines, i)

            # 추가: signature에서 접근 제한자, 한정자, 반환 타입, 파라미터 추출
            access_modifier = ""
            qualifiers = []
            return_type = ""
            parameters = []

            _cs_modifiers = {"public", "private", "protected", "internal",
                             "static", "virtual", "override", "async", "sealed", "abstract"}
            before_paren = signature.split("(")[0] if "(" in signature else signature
            tokens = before_paren.split()

            # 토큰에서 한정자, 반환 타입, 함수명 분리
            non_name_tokens = tokens[:-1] if tokens else []  # 마지막 토큰은 함수명
            for t in non_name_tokens:
                if t in _cs_modifiers:
                    qualifiers.append(t)
                    if t in ("public", "private", "protected", "internal"):
                        access_modifier = t  # 수정: 접근 제한자 추출
                else:
                    return_type = t  # 수정: 한정자가 아닌 마지막 토큰이 반환 타입

            # 파라미터 추출
            param_match = re.search(r"\(([^)]*)\)", signature)
            if param_match:
                params_str = param_match.group(1).strip()
                if params_str:
                    for p in params_str.split(","):
                        p = p.strip()
                        parts = p.rsplit(" ", 1)
                        if len(parts) == 2:
                            parameters.append({"type": parts[0], "name": parts[1]})  # 추가: 파라미터 파싱

            return FunctionInfo(
                name=target,
                class_name=changed.class_name,
                file_path=changed.file_path,
                change_type=changed.change_type.value,
                signature=signature,
                return_type=return_type,          # 추가
                parameters=parameters,            # 추가
                access_modifier=access_modifier,  # 추가
                qualifiers=qualifiers,            # 추가
                doc_comment=doc_comment,
                raw_code="\n".join(lines[i:min(i + 30, len(lines))]),
            )

    return None


# ============================================================
# 공통 헬퍼
# ============================================================
def _walk_tree(node):
    """AST 노드를 재귀적으로 순회한다."""
    yield node
    for child in node.children:
        yield from _walk_tree(child)


def _parse_cpp_param(param_str: str) -> dict:
    """C++ 파라미터 문자열을 {type, name}으로 파싱한다."""
    param_str = param_str.strip()

    # 기본값 제거
    if "=" in param_str:
        param_str = param_str.split("=")[0].strip()

    parts = param_str.rsplit(" ", 1)
    if len(parts) == 2:
        return {"type": parts[0].strip(), "name": parts[1].strip("&*")}

    return {"type": param_str, "name": ""}


def _extract_doc_comment(lines: list[str], func_start: int) -> str:
    """함수 선언 직전의 주석 블록을 추출한다. (@brief, ///, /** 등)"""
    comment_lines = []
    i = func_start - 1

    while i >= 0:
        stripped = lines[i].strip()

        if not stripped:
            i -= 1
            continue

        if any(stripped.startswith(prefix) for prefix in ("///", "//", "*", "/**", "@", "// ---")):
            comment_lines.append(stripped)
            i -= 1
            continue

        if stripped == "// ----------------------------------------":
            comment_lines.append(stripped)
            i -= 1
            continue

        break

    return "\n".join(reversed(comment_lines))


def _extract_cpp_qualifiers(lines: list[str], func_start: int) -> list[str]:
    """함수 선언부에서 한정자(virtual, static, [[nodiscard]] 등)를 추출한다."""
    qualifiers = []

    # 함수 라인과 바로 위 라인 확인
    check_range = range(max(0, func_start - 2), min(func_start + 1, len(lines)))

    for i in check_range:
        line = lines[i].strip()
        if "virtual" in line:
            qualifiers.append("virtual")
        if "static" in line:
            qualifiers.append("static")
        if "[[nodiscard]]" in line:
            qualifiers.append("[[nodiscard]]")
        if "constexpr" in line:
            qualifiers.append("constexpr")
        if "inline" in line:
            qualifiers.append("inline")
        if "explicit" in line:
            qualifiers.append("explicit")
        if "override" in line:
            qualifiers.append("override")
        if "const" in line and "(" not in line:
            qualifiers.append("const")

    return list(set(qualifiers))


def _detect_cpp_access(lines: list[str], func_start: int) -> str:
    """함수 위쪽을 거슬러 올라가며 가장 가까운 접근 제한자를 찾는다."""
    for i in range(func_start - 1, -1, -1):
        stripped = lines[i].strip()
        if stripped.startswith("public:"):
            return "public"
        if stripped.startswith("private:"):
            return "private"
        if stripped.startswith("protected:"):
            return "protected"
        if stripped.startswith("class ") or stripped.startswith("struct "):
            return "private"  # class 기본
    return ""
