#pragma once

#include "stdafx.h"

class Socket
{
private:
#ifndef LIBRETRO
	#ifdef _WIN32
	bool _cleanupWSA = false;
	#endif
	
	uintptr_t _socket = ~0;
	bool _connectionError = false;
	char* _sendBuffer;
	int _bufferPosition;
	int32_t _UPnPPort = -1;
#endif

public:
	Socket();
	Socket(uintptr_t socket);
	~Socket();

	void SetSocketOptions();
	void SetConnectionErrorFlag();

	void Close();
	bool ConnectionError();
	
	bool Connect(const char* hostname, uint16_t port);

	// server
	void Bind(uint16_t port);
	void Listen(int backlog);
	shared_ptr<Socket> Accept();

	int Send(char *buf, int len, int flags);
	void BufferedSend(char *buf, int len);
	void SendBuffer();
	int Recv(char *buf, int len, int flags);


#ifndef INET6_ADDRSTRLEN
	#define INET6_ADDRSTRLEN 46
#endif

	struct AddrInfo
	{
		char addr_buff[INET6_ADDRSTRLEN];
		int family;
		int socktype;	
		int protocol;	
		int addr_len {0};
	};

	void InitSocket(const AddrInfo &addr_info);
};
