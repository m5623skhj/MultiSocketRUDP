#pragma once
#include "../Common/etc/CoreType.h"

class RUDPSession;

struct IOContext;

class IIOHandler
{
public:
  virtual ~IIOHandler() = default;

public:
  [[nodiscard]]
  virtual bool IOCompleted(IOContext* context, const unsigned long transferred, const ThreadIdType threadId) const = 0;
  [[nodiscard]]
  virtual bool DoRecv(const RUDPSession& session) const = 0;
  [[nodiscard]]
  virtual bool DoSend(OUT RUDPSession& session, const ThreadIdType threadId) const = 0;
};
