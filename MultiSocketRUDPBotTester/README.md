# MultiSocketRUDPBotTester

## 제작 기간 : 2025.10.17 ~ 진행중

1. 개요

---

1. 개요

[MultiSocketRUDP](https://github.com/m5623skhj/MultiSocketRUDP)의 봇 테스트를 진행하기 위한 서브 프로젝트입니다.  
일반 유저들이 사용하기 편하도록 보드에 노드 트리 그래프를 직접 그리거나, 혹은 AI에게 맡겨서 작성하는 것을 목표로 합니다.

1.1 메인 페이지 UI 구성  
<img width="782" height="385" alt="image" src="https://github.com/user-attachments/assets/ea8d54ff-bbf4-4e84-aee3-83a8e97f5c8c" />  
* Set Bot Action Graph : 사용자가 봇의 행동을 정의하도록 봇 액션 그래프를 그릴 수 있는 보드 및 노드를 제공
* Start Bot Test : 액션 그래프가 작성되어 있다면, 해당 그래프에 따라 봇이 행동을 시작
* Stop Bot Test : 봇 테스트 중지

1.2 Start Bot Action Graph UI 구성
<img width="1913" height="1024" alt="image" src="https://github.com/user-attachments/assets/102d3c69-890f-45de-b52a-b9bad51ca159" />  
* AI Tree Generator : AI에게 액션 노드 트리를 구성하도록 요청
* Add Node : 좌측의 노드들 중 현재 선택되어 있는 노드를 보드에 추가
* Vaildate Graph : 그래프의 정합성을 검증
* Build Graph : 그래프를 빌드
* Apply to BotTester : 빌드된 봇 테스트를 적용, 해당 버튼 클릭 이후 부터 메인 페이지의 Start Bot Test의 진행이 현재 구성으로 변경됨
* View Statistics : 노드 통계 창을 출력

1.2.1 AI를 이용한 액션 그래프 제작  
![bandicam2026-02-0200-29-17-679-ezgif com-video-to-gif-converter](https://github.com/user-attachments/assets/94b2be91-f57c-4a48-9d1e-067577a2acc5)  
봇 테스트 노드를 직접 그리기 어려울 경우, 원하는 테스트 내용을 입력하면 AI에게 요청하여 테스트 노드 트리를 생성할 수 있습니다.  
단, 이 기능을 사용할 경우, AI가 원하는 테스트 내용을 정상적으로 생성했는지에 대한 확인이 필요합니다.  

1.2.2 노드 통계 창  
<img width="883" height="584" alt="image" src="https://github.com/user-attachments/assets/115d479e-13cd-4009-810a-b74895efb1fb" />
