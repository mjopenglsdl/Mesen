#pragma once
#include "stdafx.h"
#include <thread>
#include <vector>
#include <utility>
#include <functional>
#include <memory>

#include "INotificationListener.h"
#include "../Utilities/Timer.h"

class Socket;
class GameClientConnection;
class ClientConnectionData;
class Console;

class GameClient : public INotificationListener
{
private:
	struct InvervalCallInfo
	{
		int call_interval;
		int prev_call_time{0};
		std::function<void()> fn;

		InvervalCallInfo(int call_interval, std::function<void()> fn) : call_interval(call_interval), fn(fn) {}
	};

	static shared_ptr<GameClient> _instance;

	shared_ptr<Console> _console;
	unique_ptr<std::thread> _clientThread;
	atomic<bool> _stop;

	shared_ptr<GameClientConnection> _connection;
	bool _connected = false;

	Timer _timer; 
	std::vector<InvervalCallInfo> _interval_calls;

	static shared_ptr<GameClientConnection> GetConnection();

	void ProcesIntervalCall();
	void RegIntervalCall(int millisecond, std::function<void()>);

	void PrivateConnect(ClientConnectionData &connectionData);
	void Exec();

public:
	GameClient(shared_ptr<Console> console);
	virtual ~GameClient();

	static bool Connected();
	static void Connect(shared_ptr<Console> console, ClientConnectionData &connectionData);
	static void Disconnect();

	static void SelectController(uint8_t port);
	static uint8_t GetControllerPort();
	static uint8_t GetAvailableControllers();

	void ProcessNotification(ConsoleNotificationType type, void* parameter) override;
};