#pragma once

#include "net/messages/NetMessage.h"

class PingMessage : public NetMessage
{
private:
	uint32_t _id;

protected:
	void ProtectedStreamState() override
	{
		Stream<uint32_t>(_id);
	}

public:
	PingMessage(void* buffer, uint32_t length) : NetMessage(buffer, length) { }
	PingMessage(uint32_t id) : _id(id), NetMessage(MessageType::Ping) {}

	uint32_t GetId() {
		return _id;
	}
};