#pragma once

#include <unordered_map>
#include <deque>

#include "GameConnection.h"
#include "../Utilities/AutoResetEvent.h"
#include "../Utilities/SimpleLock.h"
#include "BaseControlDevice.h"
#include "IInputProvider.h"
#include "ControlDeviceState.h"
#include "ClientConnectionData.h"
#include "../Utilities/Timer.h"

class Console;
class LockstepManager;

class GameClientConnection : public GameConnection, public INotificationListener, public IInputProvider
{
private:
	std::deque<ControlDeviceState> _inputData[BaseControlDevice::PortCount];
	atomic<uint32_t> _inputSize[BaseControlDevice::PortCount];
	AutoResetEvent _waitForInput[BaseControlDevice::PortCount];
	SimpleLock _writeLock;
	atomic<bool> _shutdown;
	atomic<bool> _enableControllers;
	atomic<uint32_t> _minimumQueueSize;

	vector<PlayerInfo> _playerList;

	shared_ptr<BaseControlDevice> _controlDevice;
	shared_ptr<BaseControlDevice> _newControlDevice;
	ControlDeviceState _lastInputSent;
	bool _gameLoaded = false;
	uint8_t _controllerPort = GameConnection::SpectatorPort;
	ClientConnectionData _connectionData;
	string _serverSalt;

	Timer _timer;

	// ping 
	std::unordered_map<uint32_t, int> m_ping_histroy;

private:
	void SendHandshake();
	void SendControllerSelection(uint8_t port);
	void ClearInputData();
	void PushControllerState(uint8_t port, ControlDeviceState state);
	void DisableControllers();
	bool AttemptLoadGame(string filename, uint32_t crc32Hash);


protected:
	void ProcessMessage(NetMessage* message) override;

public:
	GameClientConnection(shared_ptr<Console> console, shared_ptr<Socket> socket, ClientConnectionData &connectionData);
	virtual ~GameClientConnection();

	void Shutdown();

	void ProcessNotification(ConsoleNotificationType type, void* parameter) override;

	bool SetInput(BaseControlDevice *device) override;
	void InitControlDevice();

	void SendInput();
	void SendInputTurn();
	void SendPing();

	void SelectController(uint8_t port);
	uint8_t GetAvailableControllers();
	uint8_t GetControllerPort();

/// lockstep
private:
	int turn_num_{-2};
    int sub_turn_num_{0};
};