import os
import requests
import google.generativeai as genai

MAX_LINES = 1000

api_key = os.environ["GEMINI_API_KEY"]
pr_number = os.environ["PR_NUMBER"]
repo = os.environ["REPO"]
github_token = os.environ["GITHUB_TOKEN"]

genai.configure(api_key=api_key)
model = genai.GenerativeModel("gemini-2.5-flash")

system_instruction = """
당신은 고도로 훈련된 시니어 코드 리뷰 전문가입니다.
코드를 수정하지 말고 리뷰와 주석 제안만 작성하세요.
각 항목은 file, line, comment 필드를 가져야 합니다.
한국어만 사용하세요.
장황한 설명을 하지 마세요.
"""

with open("diff.txt", "r", encoding="utf-8", errors="ignore") as f:
    diff = f.read()

line_count = diff.count("\n")
if line_count == 0:
    print("No target file changes.")
    exit(0)

if line_count > MAX_LINES:
    print("Diff too large, skipping.")
    exit(0)

if len(diff) > 20000:
    diff = diff[:20000]

prompt = f"""
다음 PR 변경 코드에서 어떤 기능들이 수정 되었는지 한국어로 간단한 리뷰를 작성하세요.
추가적으로 각 클래스 및 추가된 함수들에 대해서 주석이 달려 있지 않다면, 해당 기능에 대한 주석을 작성하세요.
코드는 수정하지 말고 설명과 개선 제안만 작성하세요.

{diff}
"""

response = model.generate_content(prompt)
comment_body = response.text[:60000]

url = f"https://api.github.com/repos/{repo}/issues/{pr_number}/comments"
headers = {
    "Authorization": f"Bearer {github_token}",
    "Accept": "application/vnd.github+json"
}
data = {
    "body": comment_body
}

requests.post(url, headers=headers, json=data)
