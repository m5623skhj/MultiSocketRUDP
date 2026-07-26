# PacketUploader

> **`PacketDefine.yml`의 패킷 목록을 Google Sheets worksheet에 업로드한다.**  
> service account 인증을 사용하며, 업로드 전에 대상 worksheet 전체를 비운다.

---

## 목차

1. [실행](#실행)
2. [설정](#설정)
3. [변환 형식](#변환-형식)
4. [외부 상태 변경](#외부-상태-변경)

---

## 실행

```batch
MultiSocketRUDP\Tool\PacketUploader.bat
```

생성과 업로드를 연속 실행하려면 아래 스크립트를 사용한다.

```batch
MultiSocketRUDP\Tool\PacketGenerateAndUploader.bat
```

두 번째 스크립트는 `PacketGenerate.bat nopause`가 끝난 뒤 uploader를 호출한다.

---

## 설정

`MultiSocketRUDP/Tool/PacketUploader/config.json`은 아래 키를 사용한다.

```json
{
  "spreadsheet_id": "<Google Sheets document id>",
  "sheet_name": "PacketDefine",
  "yaml_file": "..\\PacketDefine.yml",
  "auth_file": "credentials.json"
}
```

| 키 | 설명 |
|----|------|
| `spreadsheet_id` | 열 대상 spreadsheet ID |
| `sheet_name` | 갱신할 worksheet 이름 |
| `yaml_file` | uploader 디렉터리 기준 입력 YAML 경로 |
| `auth_file` | uploader 디렉터리 기준 service account credential JSON |

Python 환경에는 `PyYAML`, `gspread`, `google-auth`가 필요하다. 대상 spreadsheet는 service account 계정에 편집 권한으로 공유되어 있어야 한다.

> credential JSON과 실제 API key는 문서·로그·커밋에 노출하지 않는다.

---

## 변환 형식

첫 행은 아래 header다.

```text
Type | PacketName | Description | ItemType | ItemName
```

- packet의 `Type`, `PacketName`, `Desc`를 각 행에 기록한다.
- `Items`가 있으면 item마다 한 행을 만든다.
- `Items`가 없으면 `ItemType`, `ItemName`에 `-`를 기록한다.

---

## 외부 상태 변경

```text
worksheet.clear()
→ worksheet.update("A1", dataRows)
```

업로드는 부분 갱신이 아니다. 대상 worksheet의 기존 셀을 전부 지운 뒤 새 데이터를 기록한다. 잘못된 `spreadsheet_id`나 `sheet_name`을 사용하면 의도하지 않은 worksheet 내용을 덮어쓸 수 있으므로 실행 전에 설정과 공유 대상을 확인해야 한다.

---

## 관련 문서

- [[PacketGenerator]] - 입력 YAML과 생성 코드
- [[DevelopmentScripts]] - 관련 batch 스크립트
