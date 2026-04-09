"""
docs_bot 메인 모듈.
전체 파이프라인을 오케스트레이션한다.
Phase 3: 변경 감지 → 코드 추출 → 문서 매핑 → AI 분석 → 문서 수정 → PR 생성.
"""

import sys
import os
import logging
from datetime import datetime, timezone

from config import (
    DOCS_BOT_LAST_RUN, REPO, TARGET_EXTENSIONS,
    DOCS_DIR, STYLE_GUIDE_PATH,
    AI_PROVIDER, AI_API_KEY, AI_MODEL,
    BOT_BRANCH_PREFIX, BOT_PR_TITLE_PREFIX,
)
from git_utils import (
    get_last_run_time, get_current_time_iso, get_merged_prs,
    get_file_content, close_existing_bot_prs,
    create_branch, commit_and_push, create_pr,
)
from change_detector import detect_changes
from code_extractor import extract_all
from doc_mapper import map_all, group_by_section
from ai_client import create_client, analyze_all
from doc_writer import apply_all_changes, build_pr_body

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
logger = logging.getLogger("docs_bot")

EXIT_SUCCESS = 0
EXIT_INITIAL_RUN = 0
EXIT_NO_CHANGES = 0
EXIT_FAILURE = 1


def set_output(key: str, value: str):
    """GitHub Actions 출력 변수를 설정한다. 수정: deprecated ::set-output → $GITHUB_OUTPUT 파일 방식."""
    github_output = os.environ.get("GITHUB_OUTPUT")
    if github_output:
        with open(github_output, "a") as f:
            f.write(f"{key}={value}\n")
    else:
        # 로컬 테스트용 fallback
        print(f"[OUTPUT] {key}={value}")


def main() -> int:
    """
    docs_bot 메인 파이프라인.

    1. DOCS_BOT_LAST_RUN 조회 (없으면 초기값 설정 후 종료)
    2. 머지된 PR 수집 → 없으면 즉시 종료
    3. 기존 봇 PR 닫기
    4. 변경 감지 + 코드 추출
    5. 문서 매핑
    6. AI 분석 (비교 / 생성)
    7. 문서 수정
    8. 변경 있으면 브랜치 생성 → 커밋 → PR 생성
    9. DOCS_BOT_LAST_RUN 갱신
    """
    logger.info("=" * 60)
    logger.info("docs_bot 시작")
    logger.info("=" * 60)

    # Step 1: DOCS_BOT_LAST_RUN 조회
    last_run = get_last_run_time(DOCS_BOT_LAST_RUN)
    if last_run is None:
        logger.info("초기 실행: 기준 시간 설정 후 종료")
        set_output("initial_run", "true")
        set_output("current_time", get_current_time_iso())
        return EXIT_INITIAL_RUN

    # Step 2: 머지된 PR 수집
    logger.info("머지된 PR 수집 중... (since: %s)", last_run.isoformat())
    merged_prs = get_merged_prs(last_run)
    if not merged_prs:
        logger.info("머지된 PR 없음 → 종료")
        set_output("has_changes", "false")
        return EXIT_NO_CHANGES

    logger.info("수집된 PR: %d개", len(merged_prs))
    for pr in merged_prs:
        logger.info("  - PR #%d: %s (%d files)", pr.number, pr.title, len(pr.changed_files))

    # Step 3: 기존 봇 PR 닫기
    closed = close_existing_bot_prs()
    if closed:
        logger.info("기존 봇 PR %d개 닫기 완료", closed)

    # Step 4: 변경 감지 + 코드 추출
    logger.info("변경 감지 중...")
    all_changed_files = set()
    for pr in merged_prs:
        for f in pr.changed_files:
            if any(f.endswith(ext) for ext in TARGET_EXTENSIONS):
                all_changed_files.add(f)

    file_contents: dict[str, str | None] = {}
    for fp in all_changed_files:
        file_contents[fp] = get_file_content(fp)

    changes = detect_changes(merged_prs, file_contents)
    if not changes:
        logger.info("인터페이스 변경 없음 → 종료")
        set_output("has_changes", "false")
        return EXIT_NO_CHANGES

    logger.info("감지된 변경: %d개", len(changes))
    function_infos = extract_all(changes, file_contents)
    logger.info("코드 추출 완료: %d개", len(function_infos))

    # Step 5: 문서 매핑
    logger.info("문서 매핑 중...")
    docs_files = _load_docs_files()
    mappings = map_all(function_infos, docs_files)

    success_mappings = [m for m in mappings if m.mapping_status == "success"]
    no_doc_mappings = [m for m in mappings if m.mapping_status == "no_doc"]
    no_section_mappings = [m for m in mappings if m.mapping_status == "no_section"]
    excluded_mappings = [m for m in mappings if m.mapping_status == "excluded"]

    section_groups = group_by_section(mappings)
    added_infos = [
        m.function_info for m in mappings
        if m.function_info.change_type == "added" and m.mapping_status == "no_doc"
    ]
    deleted_mappings = [
        m for m in mappings
        if m.function_info.change_type == "deleted" and m.mapping_status == "success"
    ]

    logger.info("매핑: 성공 %d, 문서없음 %d, 섹션없음 %d, 제외 %d",
                len(success_mappings), len(no_doc_mappings),
                len(no_section_mappings), len(excluded_mappings))
    logger.info("AI 분석 대상: 비교 %d그룹, 생성 %d, 삭제 %d",
                len(section_groups), len(added_infos), len(deleted_mappings))

    # Step 6: AI 분석
    isAIConfigError = not AI_PROVIDER or not AI_API_KEY
    if not AI_PROVIDER:
        logger.warning("AI_PROVIDER  → AI 분석 스킵")  # 수정: error → warning
        logger.info("환경 변수를 설정하세요: AI_PROVIDER")

    if not AI_API_KEY:
        logger.warning("AI_API_KEY 미설정 → AI 분석 스킵")  # 수정: error → warning
        logger.info("환경 변수를 설정하세요: AI_API_KEY")

    if isAIConfigError:
        _print_summary(section_groups, added_infos, deleted_mappings, excluded_mappings)  # 추가: 결과 요약 출력
        set_output("has_changes", "true")
        set_output("ai_skipped", "true")
        return EXIT_SUCCESS  # 수정: EXIT_FAILURE → EXIT_SUCCESS (AI 미설정은 에러가 아님)

    logger.info("AI 분석 시작: provider=%s", AI_PROVIDER)
    client = create_client(AI_PROVIDER, AI_API_KEY, AI_MODEL)

    style_guide = docs_files.get(STYLE_GUIDE_PATH, "") or get_file_content(STYLE_GUIDE_PATH) or ""
    if style_guide:
        logger.info("스타일 가이드 로드 완료 (%d자)", len(style_guide))

    analysis_results, generate_results = analyze_all(
        client=client,
        section_groups=section_groups,
        added_infos=added_infos,
        style_guide=style_guide,
        docs_files=docs_files,
    )

    # Step 7: 문서 수정
    logger.info("문서 수정 적용 중...")
    file_changes = apply_all_changes(
        docs_files=docs_files,
        analysis_results=analysis_results,
        generate_results=generate_results,
        deleted_mappings=deleted_mappings,
        section_groups=section_groups,
        mappings=mappings,
    )

    if not file_changes:
        logger.info("문서 변경 없음 → 종료")
        set_output("has_changes", "false")
        return EXIT_NO_CHANGES

    # Step 8: 브랜치 생성 → 커밋 → PR 생성
    logger.info("PR 생성 중... (%d개 파일 변경)", len(file_changes))

    now_str = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    branch_name = f"{BOT_BRANCH_PREFIX}{now_str}"

    if not create_branch(branch_name):
        logger.error("브랜치 생성 실패")
        return EXIT_FAILURE

    date_str = datetime.now(timezone.utc).strftime("%Y-%m-%d")
    commit_msg = f"{BOT_PR_TITLE_PREFIX} {date_str} 문서 자동 갱신"

    if not commit_and_push(branch_name, file_changes, commit_msg):
        logger.error("커밋/푸시 실패")
        return EXIT_FAILURE

    pr_title = f"{BOT_PR_TITLE_PREFIX} {date_str} 문서 자동 갱신"
    pr_body = build_pr_body(
        analysis_results=analysis_results,
        generate_results=generate_results,
        deleted_mappings=deleted_mappings,
        excluded_mappings=excluded_mappings,
        no_doc_mappings=no_doc_mappings,
        no_section_mappings=no_section_mappings,
        source_prs=merged_prs,
    )

    pr_number = create_pr(branch_name, pr_title, pr_body)
    if not pr_number:
        logger.error("PR 생성 실패")
        return EXIT_FAILURE

    logger.info("PR #%d 생성 완료: %s", pr_number, pr_title)

    # Step 9: 결과 요약
    updates = [r for r in analysis_results if r.needs_update and not r.is_error]
    errors = [r for r in analysis_results if r.is_error]
    gen_ok = [r for r in generate_results if not r.is_error]

    logger.info("=" * 60)
    logger.info("완료: PR #%d 생성", pr_number)
    logger.info("  수정: %d, 생성: %d, 삭제: %d, 실패: %d",
                len(updates), len(gen_ok), len(deleted_mappings), len(errors))
    logger.info("=" * 60)

    set_output("has_changes", "true")
    set_output("pr_number", str(pr_number))
    return EXIT_SUCCESS


def _load_docs_files() -> dict[str, str]:
    docs_files: dict[str, str] = {}
    if os.path.isdir(DOCS_DIR):
        for root, dirs, files in os.walk(DOCS_DIR):
            for f in files:
                if f.endswith(".md"):
                    rel = os.path.join(root, f)
                    try:
                        with open(rel, "r", encoding="utf-8") as fh:
                            docs_files[rel] = fh.read()
                    except OSError:
                        pass
        logger.info("로컬 문서 로드: %d개", len(docs_files))
    else:
        import requests
        from config import GITHUB_API_BASE, MAIN_BRANCH
        from git_utils import safe_request, gh_headers
        url = f"{GITHUB_API_BASE}/repos/{REPO}/git/trees/{MAIN_BRANCH}?recursive=1"
        r = safe_request(requests.get, url)
        if r:
            for item in r.json().get("tree", []):
                path = item["path"]
                if path.startswith(DOCS_DIR + "/") and path.endswith(".md"):
                    content = get_file_content(path)
                    if content:
                        docs_files[path] = content
        logger.info("GitHub API 문서 로드: %d개", len(docs_files))
    return docs_files


def _print_summary(section_groups, added_infos, deleted_mappings, excluded_mappings):
    """AI 미연동 시 결과 요약 로그 출력"""
    logger.info("=" * 60)
    logger.info("결과 요약 (AI 미연동)")
    logger.info("=" * 60)
    logger.info(" 비교 분석 대상: %d개 섹션", len(section_groups))
    for g in section_groups:
        funcs = ", ".join(f.name for f in g.function_infos)
        logger.info("   - %s [%s] -> %s", g.doc_file_path, g.section.heading[:40], funcs)
    logger.info("   신규 생성 대상: %d개", len(added_infos))
    for info in added_infos:
        logger.info("   - %s::%s", info.class_name, info.name)
    logger.info("   삭제 대상: %d개", len(deleted_mappings))
    for m in deleted_mappings:
        logger.info("   - %s::%s", m.function_info.class_name, m.functionn_info.name)
    logger.info("   아키텍처 플래그: %d건", len(excluded_mappings))
    for m in excluded_mappings:
        logger.info("   - %s -> %s", m.function_info.name, m.function_info.doc_file_path)


if __name__ == "__main__":
    sys.exit(main())
