# MultiSocketRUDP

## 제작 기간 : 2024.10.20 ~ 진행중

1. 개요
2. 구성
3. Tools

---

1. 개요

[이전에 개발중이던던 RUDP 프로젝트](https://github.com/m5623skhj/RUDPServer)에서 부족한 점을 보완하기 위해 생성한 프로젝트 입니다.

이전에 개발중이던 RUDP에서는 하나의 소켓이 여러 클라이언트에 대한 수신을, SendWorkerThread가 소유한 소켓에 대해서 클라이언트에 대한 송신을 담당하였는데,

이 경우 Send를 할 때, 하나의 소켓만 사용하게 되어 스레드를 효율적으로 사용하지 못하게 되고, 때문에 현재와 같이 클라이언트와 소켓이 1:1 대응이 되도록 수정하였습니다.

아래는 약식으로 그린 Connect, Recv, Send의 흐름도 입니다.

![RUDP-페이지-3 drawio](https://github.com/user-attachments/assets/d7fefb55-c949-4fdd-9ba0-04cae1749b9b)

![RUDP-페이지-1 drawio](https://github.com/user-attachments/assets/8b7e2985-e021-4383-b9ec-8fe81e9061b5)

![RUDP-페이지-2 drawio](https://github.com/user-attachments/assets/913fd4c0-0655-4f3e-b290-2bb2662490c3)

---

2. 구성

---

2.1 서버

* MultiSocketRUDPCore
  * UDP와 RIO를 사용하는 서버의 본체입니다.
  * 스레드는 아래와 같이 구성됩니다.
    * WorkerThread : IO 처리를 담당합니다.
    * RecvLogicThread : 클라이언트에게 받은 패킷을 바탕으로 연결, 연결 해제, 패킷 핸들러 호출 등을 담당합니다.
    * SessionBrokerThread : 클라이언트가 어떤 소켓에 연결을 할 것인지 알 수 있도록 지원해주는 스레드입니다.
    * RetransmissionThread : 패킷 유실 등으로 인한 타임 아웃이 발생했을 때, 해당 패킷을 재전송해주는 스레드입니다.
      * 일정 횟수 재전송을 해보고, 응답이 오지 않을 경우, 클라이언트가 끊겼다고 판단하고, 해당 세션 목록들을 TimeoutThread에 이벤트로 전달합니다.
    * TimeoutThread : RetransmissionThread에서 타임아웃 되었다고 판단되는 세션들을 정리하는 스레드입니다.
      * 단일 스레드입니다.
  
* RUDPSession
  * Core에서 관리되고 있는 각 세션의 정보를 담고 있는 객체입니다.
  * 하나의 UDP 소켓을 가지고 있으며, 논리적으로는 1개의 클라이언트를 연결합니다.
  * 서버가 초기화 될 때, 유저가 지정한 개수로 생성되며, 생성될 때, 가지고 있는 소켓을 미리 열어 놓습니다.
    * 이 부분은 '클라이언트가 세션을 요청할 때, 세션을 열고, 논리적 연결이 종료되면 소켓을 닫는다'로 변경될 예정입니다.
  
* RUDPSessionBroker
  * 최초로 클라이언트와 연결하여, 유저에게 실제로 연결될 세션을 알려주는 역할을 담당합니다.
  * BuildConfig.h의 USE_IOCP_SESSION_BROKER 값으로 IOCP를 사용하는 세션 브로커를 사용할지, 단일 스레드 세션 브로커를 사용할지 조정 가능합니다.
    * 기본적으로는 단일 스레드 세션 브로커를 사용합니다.
  * 유저가 접속하면, 세션 브로커는 연결할 세션 정보와 세션 키를 발급하고 연결을 종료시킵니다.
    * 위에서 얻은 정보로 클라이언트가 패킷을 송신하면, 서버에서는 예약된 세션키를 비교하고 RUDPSession을 해당 클라이언트에게 귀속 시킵니다.

* EssentialHandlerManager
  * 필수적인 핸들러들을 등록 및 검사하는 싱글턴 객체입니다.
  * 필수 핸들러들 중 등록되지 않은 핸들러가 존재한다면, 서버를 띄우는 것이 실패하게 됩니다.
    * 시그니쳐는 RUDPCoreEssentialFunction = std::function<bool(RUDPSession&)>이며, 해당 형태의 핸들러만 등록됩니다.
	* 핸들러는 Register~(RUDPCoreEssentialFunction&)로 등록합니다.
	* 현재 필수 등록 핸들러 목록
	  * ConnectHandler
	  * DisconnectHandler

* Ticker와 TimerEvent
  * 일정 시간 마다 등록한 이벤트를 호출하는 객체입니다.
  * Ticker는 싱글턴 객체이며, Ticker에 TimerEvent를 등록하여 사용합니다.
  * Ticker는 스레드를 하나 생성하고, 이 스레드를 사용하여 TimerEvent의 시간을 확인하고 함수를 호출합니다.
  * TimerEvent를 사용할 경우, 반드시 TimerEventCreator::Create()를 통해 생성해야 하며, Ticker::RegisterTimerEvent()를 통해 Ticker에 등록해야 정상 동작합니다.

---

2.2 클라이언트

* RUDPClientCore
	* UDP를 이용한 클라이언트 본체입니다.
	* 스레드는 아래와 같이 구성됩니다.
		* RecvThread : 서버로 부터 수신 받은 패킷을 처리하는 단일 스레드입니다.
			* 수신 받은 패킷의 타입에 따라 분기하며, 일반 수신 패킷의 경우, 큐에 받아온 패킷들을 홀딩하고 있게 됩니다.
			* 클라이언트에서는 해당 큐에 쌓여있는 패킷들을 순서대로 빼내서 처리해야 합니다.
		* SendThread : 서버로의 패킷 송신을 담당하는 단일 스레드입니다. 
		* RetransmissionThread :재전송 제어를 담당하는 단일 스레드입니다.
			* 일정 횟수 이상 재전송 해보고, 여전히 응답이 없을 경우, 서버와의 연결이 끊어졌다고 판단합니다.
     			* 위의 상황 등으로 인하여 연결이 끊어졌다고 판단될 경우, 클라이언트의 모든 스레드가 정리되며, Stop 상태가 됩니다.

* SessionGetter
	* 클라이언트를 시작하면, 옵션 파일에 지정된 주소에 요청하여 연결할 주소 및 세션 정보를 얻어오는 클래스 입니다.
		* 위의 정보는 서버의 세션 브로커에게 얻어오며, 서버에서 얻지 못할 경우, 클라이언트가 정상적으로 가동하지 않습니다.
	* BuildConfig.h의 USE_IOCP_SESSION_GETTER 값으로 IOCP를 사용하는 세션 게터를 사용할지, 단일 스레드 세션 게터를 사용할지 조정 가능합니다.
		* 기본적으로는 단일 스레드 세션 게터를 사용합니다.

---

2.3 공통

* Logger
	* 로그 출력을 위한 오브젝트입니다.
		* 기본적으로 지정된 폴더에 로그 파일을 생성하며, 유저의 지정에 따라 콘솔에도 로그를 출력합니다.
		* 샘플로 들어 있는 서버나 클라이언트에 있는 LogExtension.h 파일을 참고하여 LogBase 클래스를 상속 받고 로그 클래스를 작성해주시면 됩니다.
			* 로그는 json 형식으로 출력됩니다.

* 패킷 구성
  * 위에서 부터 아래로 순차적으로 패킷 배열에 해당 내용이 들어가 있습니다.
    * Packet header 5byte
    * Packet type 1byte
    * Packet sequence 8byte
    * Packet id 4byte
    * Packet body nbyte

---

3. Tools
   1. PacketGenerator
      * PacketGenerate.bat 파일을 실행하면 PacketDefine.yml 파일을 참조하여 아래 파일들을 생성합니다.
        * PacketHandler.cpp
        * PacketHandler.h
        * Protocol.cpp
        * Protocol.h
        * PacketIdType.h
      * 각 파일 생성 경로를 Tool/PacketGenerator/PacketItemsFilePath.py에 정의하면 해당 경로에 생성됩니다.
      * PacketDefine.yml 파일의 내용이 비어있을 경우, 파일의 삭제 및 생성을 시도하지 않습니다.
      * 해당하는 파일이 없을 경우 새로 생성합니다.
        * 단, PacketHandler.cpp의 HandlePacket(RUDPSession& session, Packet& packet) 함수들은 제거되지 않으며, 해당 함수는 사용자가 직접 지워야 합니다(유저가 작성한 코드이므로, 삭제 책임도 유저가 가지도록 함).
   2. RunDebug
      * 간단하게 디버그 모드의 Contents Client와 Contents Server(테스트 용 프로젝트)를 구동하기 위해 제공되는 배치 파일입니다. 

---
