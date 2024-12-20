# MultiSocketRUDP

1. 개요
2. 구성
3. Tools

---

1. 개요

기존 RUDP 프로젝트[https://github.com/m5623skhj/RUDPServer]에서 부족한 점을 보완하기 위해 생성한 프로젝트 입니다.

기존 RUDP에서는 하나의 소켓이 여러 클라이언트에 대한 수신을, SendWorkerThread가 소유한 소켓에 대해서 클라이언트에 대한 송신을 담당하였는데,

위의 경우, 소켓에 너무 많은 부하가 발생 될 것으로 예상되어, 클라이언트와 소켓이 1:1 대응이 되도록 수정하였습니다.

---

2. 구성

* MultiSocketRUDPCore
  * UDP와 RIO를 사용하는 서버의 본체입니다.
  * 스레드는 아래와 같이 구성됩니다.
    * WorkerThread : IO 처리를 담당합니다.
    * RecvLogicThread : 클라이언트에게 받은 패킷을 바탕으로 연결, 연결 해제, 패킷 핸들러 호출 등을 담당합니다.
    * SessionBrokerThread : 클라이언트가 어떤 소켓에 연결을 할 것인지 알 수 있도록 지원해주는 스레드입니다.
    * RetransmissionThread : 패킷 유실 등으로 인한 타임 아웃이 발생했을 때, 해당 패킷을 재전송해주는 스레드입니다.
      * 일정 횟수 재전송을 해보고, 응답이 오지 않을 경우, 클라이언트가 끊겼다고 판단하고, 해당 세션을 정리합니다.
  
* RUDPSession
  * Core에서 관리되고 있는 각 세션의 정보를 담고 있는 객체입니다.
  * 하나의 UDP 소켓을 가지고 있으며, 논리적으로는 1개의 클라이언트를 연결합니다.
  * 서버가 초기화 될 때, 유저가 지정한 개수로 생성되며, 생성될 때, 가지고 있는 소켓을 미리 열어 놓습니다.
  
* RUDPSessionBroker
  * 최초로 클라이언트와 연결하여, 유저에게 실제로 연결될 소켓을 알려주는 역할을 담당합니다.
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

---
