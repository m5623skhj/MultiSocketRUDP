"""
Git/GitHub API 유틸리티 모듈.
PR 조회, 브랜치 생성, PR 생성/닫기, DOCS_BOT_LAST_RUN 관리 등을 담당한다.
기존 GeminiComment.py의 gh_headers(), safe_request() 패턴을 참고하여 구현.
"""

import json
import logging
import requests
from datetime import datetime, timezone
from dataclasses import dataclass, field

from config import (
    GITHUB_TOKEN,
    PAT,
    REPO,
    GITHUB_API_BASE,
    REQUEST_TIMEOUT,
    MAX_RETRIES,
    MAIN_BRANCH,
    BOT_BRANCH_PREFIX,
    BOT_LABEL,
    BOT_PR_TITLE_PREFIX,
)

logger = logging.getLogger(__name__)


# ============================================================
# 데이터 클래스
# ============================================================
@dataclass
class MergedPR:
    """머지된 PR의 메타데이터."""
    number: int
    title: str
    body: str
    merged_at: str
    changed_files: list[str] = field(default_factory=list)
    diff: str = ""


# ============================================================
# GitHub API 기본 유틸
# ============================================================
def gh_headers() -> dict:
    """GitHub API 요청용 헤더를 반환한다."""
    return {
        "Authorization": f"Bearer {GITHUB_TOKEN}",
        "Accept": "application/vnd.github+json",
    }


def pat_headers() -> dict:
    """PAT 기반 GitHub API 요청용 헤더를 반환한다. PR 생성 등 write 권한이 필요한 작업에 사용."""
    token = PAT or GITHUB_TOKEN  # PAT 없으면 GITHUB_TOKEN으로 fallback
    return {
        "Authorization": f"Bearer {token}",
        "Accept": "application/vnd.github+json",
    }


def safe_request(method, url: str, **kwargs) -> requests.Response | None:
    """
    GitHub API 요청을 최대 MAX_RETRIES회 재시도한다.
    성공(200, 201, 204) 시 Response 반환, 실패 시 None 반환.
    """
    kwargs.setdefault("timeout", REQUEST_TIMEOUT)
    kwargs.setdefault("headers", gh_headers())

    for attempt in range(MAX_RETRIES):
        try:
            r = method(url, **kwargs)
            if r.status_code in (200, 201, 204):
                return r
            logger.warning(
                "GitHub API %s %s → %d (attempt %d/%d)",
                method.__name__.upper(), url, r.status_code, attempt + 1, MAX_RETRIES,
            )
        except requests.RequestException as e:
            logger.warning(
                "GitHub API 요청 실패: %s (attempt %d/%d)", e, attempt + 1, MAX_RETRIES,
            )

    return None


# ============================================================
# DOCS_BOT_LAST_RUN 관리
# ============================================================
def get_last_run_time(last_run_env: str) -> datetime | None:
    """
    DOCS_BOT_LAST_RUN 환경 변수에서 마지막 실행 시간을 파싱한다.

    Args:
        last_run_env: DOCS_BOT_LAST_RUN 환경 변수 값 (ISO 형식 문자열).

    Returns:
        datetime 객체. 변수가 비어 있거나 파싱 실패 시 None 반환 (초기 실행).
    """
    if not last_run_env or not last_run_env.strip():
        logger.info("DOCS_BOT_LAST_RUN이 비어 있음 → 초기 실행")
        return None

    try:
        dt = datetime.fromisoformat(last_run_env.strip())
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=timezone.utc)
        logger.info("마지막 실행 시간: %s", dt.isoformat())
        return dt
    except ValueError as e:
        logger.error("DOCS_BOT_LAST_RUN 파싱 실패: %s → 초기 실행으로 처리", e)
        return None


def get_current_time_iso() -> str:
    """현재 UTC 시간을 ISO 형식 문자열로 반환한다."""
    return datetime.now(timezone.utc).isoformat()


# ============================================================
# 머지된 PR 조회
# ============================================================
def get_merged_prs(since: datetime) -> list[MergedPR]:
    """
    since 이후 main 브랜치에 머지된 PR 목록을 조회한다.
    봇 자체의 PR (docs-bot 라벨)은 제외한다.

    Args:
        since: 이 시간 이후 머지된 PR만 조회.

    Returns:
        MergedPR 객체 리스트.
    """
    url = f"{GITHUB_API_BASE}/repos/{REPO}/pulls"
    params = {
        "state": "closed",
        "base": MAIN_BRANCH,
        "sort": "updated",
        "direction": "desc",
        "per_page": 100,
    }

    r = safe_request(requests.get, url, params=params)
    if not r:
        logger.error("PR 목록 조회 실패")
        return []

    merged_prs = []
    for pr_data in r.json():
        # 머지되지 않은 PR 제외
        if not pr_data.get("merged_at"):
            continue

        # 봇 PR 제외
        labels = [label["name"] for label in pr_data.get("labels", [])]
        if BOT_LABEL in labels:
            continue

        merged_at = datetime.fromisoformat(pr_data["merged_at"].replace("Z", "+00:00"))
        if merged_at <= since:
            continue

        pr = MergedPR(
            number=pr_data["number"],
            title=pr_data.get("title", ""),
            body=pr_data.get("body", "") or "",
            merged_at=pr_data["merged_at"],
        )

        # 변경 파일 목록 조회
        pr.changed_files = _get_pr_changed_files(pr.number)

        # diff 조회
        pr.diff = _get_pr_diff(pr.number)

        merged_prs.append(pr)

    logger.info("%s 이후 머지된 PR: %d개", since.isoformat(), len(merged_prs))
    return merged_prs


def _get_pr_changed_files(pr_number: int) -> list[str]:
    """PR에서 변경된 파일 경로 목록을 반환한다."""
    url = f"{GITHUB_API_BASE}/repos/{REPO}/pulls/{pr_number}/files"
    r = safe_request(requests.get, url, params={"per_page": 300})
    if not r:
        return []
    return [f["filename"] for f in r.json()]


def _get_pr_diff(pr_number: int) -> str:
    """PR의 diff 텍스트를 반환한다."""
    url = f"{GITHUB_API_BASE}/repos/{REPO}/pulls/{pr_number}"
    headers = gh_headers()
    headers["Accept"] = "application/vnd.github.v3.diff"
    r = safe_request(requests.get, url, headers=headers)
    if not r:
        return ""
    return r.text


# ============================================================
# 기존 봇 PR 닫기
# ============================================================
def close_existing_bot_prs() -> int:
    """
    열려 있는 docs-bot PR을 모두 닫는다.

    Returns:
        닫은 PR 수.
    """
    url = f"{GITHUB_API_BASE}/repos/{REPO}/pulls"
    params = {
        "state": "open",
        "head": f"{REPO.split('/')[0]}:{BOT_BRANCH_PREFIX}",
        "per_page": 100,
    }

    r = safe_request(requests.get, url, params=params)
    if not r:
        return 0

    closed_count = 0
    for pr_data in r.json():
        labels = [label["name"] for label in pr_data.get("labels", [])]
        head_ref = pr_data.get("head", {}).get("ref", "")

        if BOT_LABEL in labels or head_ref.startswith(BOT_BRANCH_PREFIX):
            close_url = f"{GITHUB_API_BASE}/repos/{REPO}/pulls/{pr_data['number']}"
            result = safe_request(
                requests.patch, close_url, json={"state": "closed"}
            )
            if result:
                closed_count += 1
                logger.info("봇 PR #%d 닫기 완료", pr_data["number"])

    return closed_count


# ============================================================
# 브랜치 생성
# ============================================================
def create_branch(branch_name: str) -> bool:
    """
    main 브랜치의 최신 커밋에서 새 브랜치를 생성한다.

    Args:
        branch_name: 생성할 브랜치명 (docs-bot/YYYYMMDD-HHmmss 형식).

    Returns:
        성공 시 True, 실패 시 False.
    """
    # main 브랜치의 최신 SHA 조회
    ref_url = f"{GITHUB_API_BASE}/repos/{REPO}/git/ref/heads/{MAIN_BRANCH}"
    r = safe_request(requests.get, ref_url)
    if not r:
        logger.error("main 브랜치 SHA 조회 실패")
        return False

    sha = r.json()["object"]["sha"]

    # 새 브랜치 생성
    create_url = f"{GITHUB_API_BASE}/repos/{REPO}/git/refs"
    result = safe_request(
        requests.post,
        create_url,
        json={"ref": f"refs/heads/{branch_name}", "sha": sha},
    )

    if result:
        logger.info("브랜치 생성 완료: %s (base SHA: %s)", branch_name, sha[:8])
        return True

    logger.error("브랜치 생성 실패: %s", branch_name)
    return False


# ============================================================
# 파일 커밋 및 푸시
# ============================================================
def commit_and_push(branch_name: str, file_changes: dict[str, str], message: str) -> bool:
    """
    변경된 파일들을 지정된 브랜치에 커밋한다.
    GitHub API의 tree/commit 방식을 사용한다.

    Args:
        branch_name: 커밋 대상 브랜치명.
        file_changes: {파일 경로: 파일 내용} 딕셔너리.
        message: 커밋 메시지.

    Returns:
        성공 시 True, 실패 시 False.
    """
    if not file_changes:
        logger.info("변경 파일 없음 → 커밋 스킵")
        return False

    # 현재 브랜치의 최신 SHA 조회
    ref_url = f"{GITHUB_API_BASE}/repos/{REPO}/git/ref/heads/{branch_name}"
    r = safe_request(requests.get, ref_url)
    if not r:
        logger.error("브랜치 %s의 SHA 조회 실패", branch_name)
        return False

    base_sha = r.json()["object"]["sha"]

    # base commit의 tree SHA 조회
    commit_url = f"{GITHUB_API_BASE}/repos/{REPO}/git/commits/{base_sha}"
    r = safe_request(requests.get, commit_url)
    if not r:
        return False

    base_tree_sha = r.json()["tree"]["sha"]

    # blob 생성 및 tree 구성
    tree_items = []
    for file_path, content in file_changes.items():
        blob_url = f"{GITHUB_API_BASE}/repos/{REPO}/git/blobs"
        blob_r = safe_request(
            requests.post, blob_url,
            json={"content": content, "encoding": "utf-8"},
        )
        if not blob_r:
            logger.error("Blob 생성 실패: %s", file_path)
            return False

        tree_items.append({
            "path": file_path,
            "mode": "100644",
            "type": "blob",
            "sha": blob_r.json()["sha"],
        })

    # tree 생성
    tree_url = f"{GITHUB_API_BASE}/repos/{REPO}/git/trees"
    tree_r = safe_request(
        requests.post, tree_url,
        json={"base_tree": base_tree_sha, "tree": tree_items},
    )
    if not tree_r:
        logger.error("Tree 생성 실패")
        return False

    # commit 생성
    new_commit_url = f"{GITHUB_API_BASE}/repos/{REPO}/git/commits"
    commit_r = safe_request(
        requests.post, new_commit_url,
        json={
            "message": message,
            "tree": tree_r.json()["sha"],
            "parents": [base_sha],
        },
    )
    if not commit_r:
        logger.error("Commit 생성 실패")
        return False

    # ref 업데이트
    update_ref_url = f"{GITHUB_API_BASE}/repos/{REPO}/git/refs/heads/{branch_name}"
    update_r = safe_request(
        requests.patch, update_ref_url,
        json={"sha": commit_r.json()["sha"]},
    )
    if not update_r:
        logger.error("Ref 업데이트 실패")
        return False

    logger.info("커밋 완료: %s (%d개 파일)", message, len(file_changes))
    return True


# ============================================================
# PR 생성
# ============================================================
def create_pr(branch_name: str, title: str, body: str) -> int | None:
    """
    문서 수정 PR을 생성한다.

    Args:
        branch_name: 소스 브랜치명.
        title: PR 제목.
        body: PR 본문.

    Returns:
        생성된 PR 번호. 실패 시 None.
    """
    url = f"{GITHUB_API_BASE}/repos/{REPO}/pulls"
    r = safe_request(
        requests.post, url,
        headers=pat_headers(),
        json={
            "title": title,
            "body": body,
            "head": branch_name,
            "base": MAIN_BRANCH,
        },
    )

    if not r:
        logger.error("PR 생성 실패")
        return None

    pr_number = r.json()["number"]

    # 라벨 추가
    label_url = f"{GITHUB_API_BASE}/repos/{REPO}/issues/{pr_number}/labels"
    safe_request(requests.post, label_url, json={"labels": [BOT_LABEL]})

    logger.info("PR #%d 생성 완료: %s", pr_number, title)
    return pr_number


# ============================================================
# DOCS_BOT_LAST_RUN 갱신 (GitHub Actions에서 사용)
# ============================================================
def update_last_run_time(repo: str, variable_name: str = "DOCS_BOT_LAST_RUN") -> bool:
    """
    GitHub Repository Variable을 현재 시간으로 갱신한다.
    이 함수는 실제로는 GitHub Actions 워크플로우의 gh CLI로 처리하는 것을 권장.
    여기서는 API 방식으로 제공한다.

    Args:
        repo: "owner/repo" 형식의 리포지토리 이름.
        variable_name: 갱신할 변수 이름.

    Returns:
        성공 시 True, 실패 시 False.
    """
    now = get_current_time_iso()
    url = f"{GITHUB_API_BASE}/repos/{repo}/actions/variables/{variable_name}"
    r = safe_request(
        requests.patch, url,
        json={"name": variable_name, "value": now},
    )

    if r:
        logger.info("DOCS_BOT_LAST_RUN 갱신: %s", now)
        return True

    logger.error("DOCS_BOT_LAST_RUN 갱신 실패")
    return False


# ============================================================
# 파일 내용 조회 (main 브랜치 기준)
# ============================================================
_file_cache: dict[str, str | None] = {}


def get_file_content(file_path: str, ref: str = MAIN_BRANCH) -> str | None:
    """
    GitHub API를 통해 특정 ref의 파일 내용을 조회한다.
    동일 경로에 대해 캐싱을 수행한다.

    Args:
        file_path: 리포지토리 내 파일 경로.
        ref: 브랜치/태그/SHA. 기본값은 main.

    Returns:
        파일 내용 문자열. 실패 시 None.
    """
    import base64

    cache_key = f"{ref}:{file_path}"
    if cache_key in _file_cache:
        return _file_cache[cache_key]

    url = f"{GITHUB_API_BASE}/repos/{REPO}/contents/{file_path}?ref={ref}"
    r = safe_request(requests.get, url)

    if not r:
        _file_cache[cache_key] = None
        return None

    data = r.json()
    if "content" not in data:
        _file_cache[cache_key] = None
        return None

    content = base64.b64decode(data["content"]).decode("utf-8", errors="ignore")
    _file_cache[cache_key] = content
    return content
