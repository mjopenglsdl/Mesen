#pragma once

#include "NetMessage.h"
#include <cstdint>

class PingMessage : public NetMessage
{
protected:
	virtual void ProtectedStreamState()
	{

	}

public:
	PingMessage(void* buffer, uint32_t length) : NetMessage(buffer, length) { }

	PingMessage() : NetMessage(MessageType::Ping)
	{

	}
};