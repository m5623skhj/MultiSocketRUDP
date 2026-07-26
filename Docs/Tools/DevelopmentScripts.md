# 개발·테스트 보조 스크립트

> **`MultiSocketRUDP/Tool`의 빌드, 실행, 인증서, 로그 정리 batch 파일을 정리한다.**  
> 모든 스크립트는 Windows 환경과 저장소의 현재 상대 경로를 전제로 한다.

---

## 목차

1. [실행과 테스트](#실행과-테스트)
2. [개발용 TLS 인증서](#개발용-tls-인증서)
3. [로그 압축](#로그-압축)

---

## 실행과 테스트

### `RunDebug.bat`

`MultiSocketRUDP/x64/Debug`로 이동해 `ContentsServer.exe`와 `ContentsClient.exe`를 각각 새 프로세스로 시작한다. 빌드나 인증서 생성을 수행하지 않으므로 Debug 실행 파일이 먼저 존재해야 한다.

### `BuildCoreTestAndRunUnitTest.bat`

```text
msbuild ..\MultiSocketRUDP.sln /t:CoreTest /p:Configuration=Debug /p:Platform=x64
→ ..\x64\Debug\CoreTest.exe
```

빌드 실패 시 실행하지 않고 해당 exit code로 종료한다.

### `BuildIntegrationTestAndRun.bat`

통합 테스트 PFX가 없으면 `CreateDevTLSPfx.bat`를 먼저 호출한 뒤 `IntegrationTest` target을 Debug x64로 빌드하고 실행한다. 최종 exit code는 `IntegrationTest.exe`의 결과다.

---

## 개발용 TLS 인증서

### `CreateDevTLSCert.bat`

현재 사용자 인증서 저장소 `Cert:\CurrentUser\My`에 `CN=DevServerCert` self-signed 인증서를 만든다. 키 길이는 2048-bit이고 만료 기간은 생성 시점부터 5년이다.

### `RemoveDevTLSCert.bat`

현재 사용자 인증서 저장소에서 subject가 `CN=DevServerCert`인 인증서를 모두 제거한다.

### `CreateDevTLSPfx.bat`

통합 테스트 전용 임시 인증서를 만들고 아래 파일로 내보낸 뒤 인증서 저장소의 원본을 제거한다.

```text
MultiSocketRUDP/IntegrationTest/TestCert.pfx
```

PFX password는 batch 파일과 통합 테스트 코드가 공유하는 테스트 전용 고정값이다. 운영 인증서에 이 스크립트나 password를 사용하면 안 된다.

---

## 로그 압축

`LogCompress.bat`는 스크립트가 위치한 디렉터리 아래 `Log Folder`를 대상으로 한다.

```text
<script directory>/Log Folder/
  └─ Archive/
```

- root와 1단계 하위 폴더의 `.txt` 파일을 `tar.gz`로 압축한다.
- 압축 파일을 다시 열어 무결성을 확인한 후에만 원본 `.txt`를 삭제한다.
- 같은 날짜의 archive가 이미 있으면 해당 폴더를 건너뛴다.
- archive 이름 끝의 날짜가 오늘과 다르면 이전 archive를 삭제한다.

> **주의:** archive 보존 기간은 하루다. 장기 보존이 필요하면 스크립트 실행 전에 별도 저장소로 복사해야 한다.

`Tool/LogCompress.bat`, `ContentsServer/LogCompress.bat`, `ContentsClient/LogCompress.bat`는 같은 로직이지만 각 파일의 위치를 기준으로 서로 다른 `Log Folder`를 처리한다.

---

## 관련 문서

- [[Testing]] - 테스트 프로젝트와 CI
- [[TLSHelper]] - TLS 구현과 인증서 로딩
- [[Logger]] - 로그 생성 형식
- [[PacketGenerator]] - 패킷 코드 생성 스크립트
- [[PacketUploader]] - 생성 정의의 Google Sheets 업로드
