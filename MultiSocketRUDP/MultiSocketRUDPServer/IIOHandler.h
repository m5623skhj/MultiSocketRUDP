#pragma once
#include "../Common/etc/CoreType.h"

class RUDPSession;

struct IOContext;

// ----------------------------------------
// @brief 비동기 I/O 작업의 완료를 처리하기 위한 인터페이스입니다.
// 실제 I/O 작업의 결과(성공 여부, 전송량 등)를 받아 후속 조치를 정의합니다.
// ----------------------------------------
class IIOHandler
{
public:
  virtual ~IIOHandler() = default;

public:
  // ----------------------------------------
  // @brief 완료된 I/O 작업을 처리하는 함수입니다.
  // 컨텍스트, 전송된 바이트 수, 스레드 ID를 기반으로 작업을 완료 처리합니다.
  // @param context 완료된 I/O 작업의 컨텍스트.
  // @param transferred 전송되거나 수신된 바이트 수.
  // @param threadId 작업을 처리한 스레드의 ID.
  // @return 성공적으로 완료 처리되었는지 여부.
  // ----------------------------------------
  [[nodiscard]]
  virtual bool IOCompleted(IOContext* context, const unsigned long transferred, const ThreadIdType threadId) const = 0;
  // ----------------------------------------
  // @brief RUDP 세션에 대한 데이터 수신 작업을 시작하는 함수입니다.
  // 해당 세션에 대한 데이터 수신 로직을 정의합니다.
  // @param session 데이터를 수신할 RUDP 세션.
  // @return 수신 작업이 성공적으로 시작되었는지 여부.
  // ----------------------------------------
  [[nodiscard]]
  virtual bool DoRecv(const RUDPSession& session) const = 0;
  // ----------------------------------------
  // @brief RUDP 세션에 대한 데이터 전송 작업을 시작하는 함수입니다.
  // 해당 세션의 데이터를 외부로 전송하는 로직을 정의합니다.
  // @param session 데이터를 전송할 RUDP 세션.
  // @param threadId 작업을 처리할 스레드의 ID.
  // @return 전송 작업이 성공적으로 시작되었는지 여부.
  // ----------------------------------------
  [[nodiscard]]
  virtual bool DoSend(OUT RUDPSession& session, const ThreadIdType threadId) const = 0;
};
