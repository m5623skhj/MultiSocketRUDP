"""
docs_bot 설정 모듈.
경로 매핑, 제외 목록, 상수 등을 관리한다.
"""

import os

# ============================================================
# GitHub 환경 변수
# ============================================================
GITHUB_TOKEN = os.environ.get("GITHUB_TOKEN", "")
PAT = os.environ.get("PAT", "")
REPO = os.environ.get("REPO", "")  # "owner/repo" 형식
DOCS_BOT_LAST_RUN = os.environ.get("DOCS_BOT_LAST_RUN", "")

# ============================================================
# 브랜치 / 라벨
# ============================================================
MAIN_BRANCH = "main"
BOT_BRANCH_PREFIX = "docs-bot/"
BOT_LABEL = "docs-bot"
BOT_PR_TITLE_PREFIX = "[docs-bot]"

# ============================================================
# 경로 설정
# ============================================================
DOCS_DIR = "Docs"
STYLE_GUIDE_PATH = f"{DOCS_DIR}/style_guide.md"

# ============================================================
# 코드 디렉토리 → 문서 디렉토리 매핑
# ============================================================
CODE_TO_DOCS_DIR_MAP = {
    "MultiSocketRUDP/MultiSocketRUDPServer": f"{DOCS_DIR}/Server",
    "MultiSocketRUDP/MultiSocketRUDPClient": f"{DOCS_DIR}/Client",
    "MultiSocketRUDP/Common": f"{DOCS_DIR}/Common",
    "MultiSocketRUDP/Logger": f"{DOCS_DIR}/Logger",
    "MultiSocketRUDP/Tool": f"{DOCS_DIR}/Tools",
    "MultiSocketRUDPBotTester": f"{DOCS_DIR}/BotTester",
}

# ============================================================
# 자동화 제외 문서 목록 (아키텍처/가이드 문서)
# ============================================================
EXCLUDED_DOCS = [
    f"{DOCS_DIR}/00_Overview.md",
    f"{DOCS_DIR}/GettingStarted.md",
    f"{DOCS_DIR}/Glossary.md",
    f"{DOCS_DIR}/ContentServerGuide.md",
    f"{DOCS_DIR}/PerformanceTuning.md",
    f"{DOCS_DIR}/Troubleshooting.md",
    f"{DOCS_DIR}/README.md",
    f"{DOCS_DIR}/Diagrams/README.md",
    f"{DOCS_DIR}/Server/SessionLifecycle.md",
    f"{DOCS_DIR}/Server/SessionComponents.md",
    f"{DOCS_DIR}/Server/ThreadModel.md",
    f"{DOCS_DIR}/Server/PacketFormat.md",
    f"{DOCS_DIR}/Server/PacketProcessing.md",   # 추가: 패킷 처리 파이프라인 아키텍처 문서
    f"{DOCS_DIR}/Common/CryptoSystem.md",
    f"{DOCS_DIR}/Common/PacketFormat.md",        # 추가: 패킷 바이트 레이아웃 스펙 문서
    f"{DOCS_DIR}/BotTester/00_BotTester_Overview.md",
]

# ============================================================
# 클래스명 → 문서 파일 수동 매핑 (이름이 불일치하는 경우)
# 키: 클래스명 (소문자), 값: 문서 파일 경로
# ============================================================
CLASS_TO_DOC_OVERRIDE = {
    "actiongraph": f"{DOCS_DIR}/BotTester/Bot/BotActionGraph.md",           # 추가: ActionGraph.cs → BotActionGraph.md
    "nodecanvasrenderer": f"{DOCS_DIR}/BotTester/UI/CanvasRenderer.md",     # 추가: NodeCanvasRenderer.cs → CanvasRenderer.md
    "rudpflowcontroller": f"{DOCS_DIR}/Common/FlowController.md",           # 추가: RUDPFlowController.h → FlowController.md
}

# ============================================================
# 대상 파일 확장자
# ============================================================
CPP_HEADER_EXTENSIONS = (".h", ".hpp")
CPP_SOURCE_EXTENSIONS = (".cpp", ".cc")
CSHARP_EXTENSIONS = (".cs",)

TARGET_EXTENSIONS = CPP_HEADER_EXTENSIONS + CPP_SOURCE_EXTENSIONS + CSHARP_EXTENSIONS

# ============================================================
# GitHub API
# ============================================================
GITHUB_API_BASE = "https://api.github.com"
REQUEST_TIMEOUT = 15
MAX_RETRIES = 3

# ============================================================
# AI API 설정
# ============================================================
AI_PROVIDER = os.environ.get("AI_PROVIDER", "")       # "claude" / "openai" / "gemini"
AI_API_KEY = os.environ.get("AI_API_KEY", "")
AI_MODEL = os.environ.get("AI_MODEL", "")              # 비어 있으면 프로바이더별 기본값
