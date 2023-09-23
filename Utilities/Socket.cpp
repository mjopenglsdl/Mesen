#include "stdafx.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include "Socket.h"

#ifndef LIBRETRO
#include "UPnPPortMapper.h"
using namespace std;

#ifdef _WIN32
	#pragma comment(lib,"ws2_32.lib") //Winsock Library
	#define WIN32_LEAN_AND_MEAN
	#include <winsock2.h>
	#include <Ws2tcpip.h>
	#include <Windows.h>
#else
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <sys/ioctl.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <errno.h>
	#include <netinet/tcp.h>
	#include <netdb.h>
	#include <unistd.h>

	#define INVALID_SOCKET (uintptr_t)-1
	#define SOCKET_ERROR -1
	#define WSAGetLastError() errno
	#define SOCKADDR_IN sockaddr_in
	#define SOCKADDR sockaddr
	#define TIMEVAL timeval
	#define SD_SEND SHUT_WR
	#define closesocket close
	#define WSAEWOULDBLOCK EWOULDBLOCK
	#define ioctlsocket ioctl
#endif

#define BUFFER_SIZE 200000


static bool getAddrInfo(Socket::AddrInfo &info, const char* host, int port)
{
	int ret = 0;

    struct addrinfo hints;
    hints.ai_flags = AI_PASSIVE;        //  Socket address is intended for `bind'
    hints.ai_family = AF_UNSPEC;        // IPV4 / IPV6 
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_IP;

    struct addrinfo *res;
    if ( (ret = getaddrinfo(host, std::to_string(port).c_str(), &hints, &res)) != 0) {
        printf("get addr err: %s\n", gai_strerror(ret));
        return false;
    } else {
        info.family = res->ai_family;
        info.socktype = res->ai_socktype;
        info.protocol = res->ai_protocol;

		memcpy(info.addr_buff, res->ai_addr, res->ai_addrlen);
        info.addr_len = res->ai_addrlen;
    }
    freeaddrinfo(res);

    return true;
}

void Socket::InitSocket(const AddrInfo &addr_info)
{
	_socket = socket(addr_info.family, addr_info.socktype, addr_info.protocol);
	if(_socket == INVALID_SOCKET) {
		std::cout << "Socket creation failed. err: " << strerror(errno) << std::endl;
		SetConnectionErrorFlag();
		return;
	} 
	
	SetSocketOptions();
}

Socket::Socket()
{
	_sendBuffer = new char[BUFFER_SIZE];
	_bufferPosition = 0;

	#ifdef _WIN32	
		WSADATA wsaDat;
		if(WSAStartup(MAKEWORD(2, 2), &wsaDat) != 0) {
			std::cout << "WSAStartup failed." << std::endl;
			SetConnectionErrorFlag();
			return;
		}
		_cleanupWSA = true;
	#endif
}

Socket::Socket(uintptr_t socket) 
{
	_socket = socket;

	if(socket == INVALID_SOCKET) {
		SetConnectionErrorFlag();
	} else {
		SetSocketOptions();
	}

	_sendBuffer = new char[BUFFER_SIZE];
	_bufferPosition = 0;
}

Socket::~Socket()
{
	if(_UPnPPort != -1) {
		UPnPPortMapper::RemoveNATPortMapping(_UPnPPort, IPProtocol::TCP);
	}

	if(_socket != INVALID_SOCKET) {
		Close();
	}

	#ifdef _WIN32
		if(_cleanupWSA) {
			WSACleanup();
		}
	#endif

	delete[] _sendBuffer;
}

void Socket::SetSocketOptions()
{
	//Non-blocking mode
	u_long iMode = 1;
	ioctlsocket(_socket, FIONBIO, &iMode);
		
	//Set send/recv buffers to 256k
	int bufferSize = 0x40000;
	setsockopt(_socket, SOL_SOCKET, SO_RCVBUF, (char*)&bufferSize, sizeof(int));
	setsockopt(_socket, SOL_SOCKET, SO_SNDBUF, (char*)&bufferSize, sizeof(int));

	//Disable nagle's algorithm to improve latency
	u_long value = 1;
	setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&value, sizeof(value));

	// support ipv4 & ipv6 for windows
	int ipv6only = 0;
	if (setsockopt(_socket, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&ipv6only, sizeof(ipv6only)) != 0) {
		cout << "set ipv6only failed!";
	}
}

void Socket::SetConnectionErrorFlag()
{
	_connectionError = true;
}

void Socket::Close()
{
	std::cout << "Socket closed." << std::endl;
	shutdown(_socket, SD_SEND);
	closesocket(_socket);
	SetConnectionErrorFlag();
}

bool Socket::ConnectionError()
{
	return _connectionError;
}

bool Socket::Connect(const char* hostname, uint16_t port)
{
	bool result = false;
	printf("connect to hostname: %s, port: %d\n", hostname, port);

	// Resolve IP address for hostname
	AddrInfo addr_info;
	if(!getAddrInfo(addr_info, hostname, port)) {
		std::cout << "Failed to resolve hostname." << std::endl;
		SetConnectionErrorFlag();
		return false;

	} else {
		InitSocket(addr_info);

		// if (-1 == (_socket = socket(addr_info.family, addr_info.socktype, addr_info.protocol))) {
		// 	printf("create socket err: %s\n", strerror(errno));
		// 	return false;
		// }

		//Set socket in non-blocking mode
		u_long iMode = 1;
		ioctlsocket(_socket, FIONBIO, &iMode);

		// Attempt to connect to server
		connect(_socket, (const struct sockaddr *)addr_info.addr_buff, (socklen_t)addr_info.addr_len);

		fd_set writeSockets;
		#ifdef _WIN32
			writeSockets.fd_count = 1;
			writeSockets.fd_array[0] = _socket;
		#else		
			FD_ZERO(&writeSockets);
    		FD_SET(_socket, &writeSockets);
		#endif

		//Timeout after 3 seconds
		TIMEVAL timeout;
		timeout.tv_sec = 3;
		timeout.tv_usec = 0;

		// check if the socket is ready
		int returnVal = select((int)_socket+1, nullptr, &writeSockets, nullptr, &timeout);
		if(returnVal > 0) {
			result = true;
		} else {
			//Could not connect
			if(returnVal == SOCKET_ERROR) {
				//int nError = WSAGetLastError();				
				//std::cout << "select failed: nError " << std::to_string(nError) << " returnVal" << std::to_string(returnVal) << std::endl;			
			}
			SetConnectionErrorFlag();
		}
	}
	return result;
}


void Socket::Bind(uint16_t port)
{
	AddrInfo addr_info;
	
	// ipv6 + ipv4
	if (!getAddrInfo(addr_info, "::0", port)) {
		std::cout << "[Bind] failed to get addr." << std::endl;
		SetConnectionErrorFlag();
		return;
	}
	
	InitSocket(addr_info);

	if(UPnPPortMapper::AddNATPortMapping(port, port, IPProtocol::TCP)) {
		_UPnPPort = port;
	}

	// bind
	if(::bind(_socket, (SOCKADDR*)(&addr_info.addr_buff), addr_info.addr_len) == SOCKET_ERROR) {
		std::cout << "Unable to bind socket. err: "<<strerror(errno) << std::endl;
		SetConnectionErrorFlag();
	}
}

void Socket::Listen(int backlog)
{
	if(listen(_socket, backlog) == SOCKET_ERROR) {
		std::cout << "listen failed. err: " << strerror(errno)<< std::endl;
		SetConnectionErrorFlag();
	}
}

shared_ptr<Socket> Socket::Accept()
{
	uintptr_t socket = accept(_socket, nullptr, nullptr);
	return shared_ptr<Socket>(new Socket(socket));
}

bool WouldBlock(int nError)
{
	return nError == WSAEWOULDBLOCK || nError == EAGAIN;
}

int Socket::Send(char *buf, int len, int flags)
{
	int retryCount = 15;
	int nError = 0;
	int returnVal;
	do {
		//Loop until everything has been sent (shouldn't loop at all in the vast majority of cases)
		returnVal = send(_socket, buf, len, flags);

		if(returnVal > 0) {
			//Sent partial data, adjust pointer & length
			buf += returnVal;
			len -= returnVal;
		} else if(returnVal == SOCKET_ERROR) {
			nError = WSAGetLastError();
			if(nError != 0) {
				if(!WouldBlock(nError)) {
					SetConnectionErrorFlag();
				} else {
					retryCount--;
					if(retryCount == 0) {
						//Connection seems dead, close it.
						std::cout << "Unable to send data, closing socket." << std::endl;
						Close();
						return 0;
					}
					
					std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(10));
				}
			}
		}
	} while(WouldBlock(nError) && len > 0);
		
	return returnVal;
}

void Socket::BufferedSend(char *buf, int len)
{
	if(_bufferPosition+len < BUFFER_SIZE) {
		memcpy(_sendBuffer+_bufferPosition, buf, len);
		_bufferPosition += len;
	} else {
		std::cout << "prevented buffer overflow";
	}
}

void Socket::SendBuffer()
{
	Send(_sendBuffer, _bufferPosition, 0);
	_bufferPosition = 0;
}

int Socket::Recv(char *buf, int len, int flags)
{
	int returnVal = recv(_socket, buf, len, flags);
	
	if(returnVal == SOCKET_ERROR) {
		int nError = WSAGetLastError();
		if(nError && !WouldBlock(nError)) {
			std::cout << "recv failed: nError " << std::to_string(nError) << " returnVal" << std::to_string(returnVal) << std::endl;			
			SetConnectionErrorFlag();
		}
	} else if(returnVal == 0) {
		//Socket closed
		std::cout << "Socket closed by peer." << std::endl;
		Close();
	}

	return returnVal;
}

#else

//Libretro port does not need sockets.

Socket::Socket()
{
}

Socket::Socket(uintptr_t socket)
{
}

Socket::~Socket()
{
}

void Socket::SetSocketOptions()
{
}

void Socket::SetConnectionErrorFlag()
{
}

void Socket::Close()
{
}

bool Socket::ConnectionError()
{
	return true;
}

void Socket::Bind(uint16_t port)
{
}

bool Socket::Connect(const char* hostname, uint16_t port)
{
	return false;
}

void Socket::Listen(int backlog)
{
}

shared_ptr<Socket> Socket::Accept()
{
	return shared_ptr<Socket>(new Socket(0));
}

bool WouldBlock(int nError)
{
	return false;
}

int Socket::Send(char *buf, int len, int flags)
{
	return 0;
}

void Socket::BufferedSend(char *buf, int len)
{
}

void Socket::SendBuffer()
{
}

int Socket::Recv(char *buf, int len, int flags)
{
	return 0;
}
#endif
