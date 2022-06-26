#include "stdafx.h"
#include "GameClientConnection.h"

#include "Console.h"
#include "EmulationSettings.h"
#include "ControlManager.h"
#include "ClientConnectionData.h"
#include "StandardController.h"
#include "Zapper.h"
#include "ArkanoidController.h"
#include "BandaiHyperShot.h"
#include "NotificationManager.h"

#include "net/messages/PlayerListMessage.h"
#include "net/messages/ForceDisconnectMessage.h"
#include "net/messages/SelectControllerMessage.h"
#include "net/messages/ServerInformationMessage.h"
#include "net/messages/InputDataMessage.h"
#include "net/messages/MovieDataMessage.h"
#include "net/messages/TurnDataMessage.h"
#include "net/messages/HandShakeMessage.h"
#include "net/messages/GameInformationMessage.h"
#include "net/messages/SaveStateMessage.h"
#include "net/messages/PingMessage.h"


GameClientConnection::GameClientConnection(shared_ptr<Console> console, shared_ptr<Socket> socket, ClientConnectionData &connectionData) : GameConnection(console, socket)
{
	_connectionData = connectionData;
	_shutdown = false;
	_enableControllers = false;
	_minimumQueueSize = 3;

	MessageManager::DisplayMessage("NetPlay", "ConnectedToServer");
}

GameClientConnection::~GameClientConnection()
{
	Shutdown();
}

void GameClientConnection::Shutdown()
{
	if(!_shutdown) {
		_shutdown = true;
		DisableControllers();

		ControlManager* controlManager = _console->GetControlManager();
		if(controlManager) {
			controlManager->UnregisterInputProvider(this);
		}
		_console->GetNotificationManager()->SendNotification(ConsoleNotificationType::DisconnectedFromServer);
		MessageManager::DisplayMessage("NetPlay", "ConnectionLost");
		_console->GetSettings()->ClearFlags(EmulationFlags::ForceMaxSpeed);
	}
}

void GameClientConnection::SendHandshake()
{
	HandShakeMessage message(_connectionData.PlayerName, HandShakeMessage::GetPasswordHash(_connectionData.Password, _serverSalt), _connectionData.Spectator);
	SendNetMessage(message);
}

void GameClientConnection::SendControllerSelection(uint8_t port)
{
	SelectControllerMessage message(port);
	SendNetMessage(message);
}

void GameClientConnection::ClearInputData()
{
	LockHandler lock = _writeLock.AcquireSafe();
	for(int i = 0; i < BaseControlDevice::PortCount; i++) {
		_inputSize[i] = 0;
		_inputData[i].clear();
	}
}

void GameClientConnection::ProcessMessage(NetMessage* message)
{
	GameInformationMessage* gameInfo;
	
	switch(message->GetType()) {
		case MessageType::ServerInformation:
			_serverSalt = ((ServerInformationMessage*)message)->GetHashSalt();
			SendHandshake();
			break;

		case MessageType::SaveState:
			if(_gameLoaded) {
				DisableControllers();
				_console->Pause();
				ClearInputData();
				((SaveStateMessage*)message)->LoadState(_console);
				_enableControllers = true;
				InitControlDevice();
				_console->Resume();
			}
			break;

		case MessageType::MovieData:
			if(_gameLoaded) {
				PushControllerState(((MovieDataMessage*)message)->GetPortNumber(), ((MovieDataMessage*)message)->GetInputState());
			}
			break;

		case MessageType::ForceDisconnect:
			MessageManager::DisplayMessage("NetPlay", ((ForceDisconnectMessage*)message)->GetMessage());
			break;

		case MessageType::PlayerList:
			_playerList = ((PlayerListMessage*)message)->GetPlayerList();
			break;

		case MessageType::GameInformation:
			DisableControllers();
			_console->Pause();
			gameInfo = (GameInformationMessage*)message;
			if(gameInfo->GetPort() != _controllerPort) {
				_controllerPort = gameInfo->GetPort();

				if(_controllerPort == GameConnection::SpectatorPort) {
					MessageManager::DisplayMessage("NetPlay", "ConnectedAsSpectator");
				} else {
					MessageManager::DisplayMessage("NetPlay", "ConnectedAsPlayer", std::to_string(_controllerPort + 1));
				}
			}

			ClearInputData();

			_gameLoaded = AttemptLoadGame(gameInfo->GetRomFilename(), gameInfo->GetCrc32Hash());
			if(gameInfo->IsPaused()) {
				_console->GetSettings()->SetFlags(EmulationFlags::Paused);
			} else {
				_console->GetSettings()->ClearFlags(EmulationFlags::Paused);
			}
			_console->Resume();
			break;

		case MessageType::Ping:
			{
				uint32_t ping_id = ((PingMessage*)message)->GetId();
				if (m_ping_histroy.find(ping_id) != m_ping_histroy.end()) {
					int ping = static_cast<int>(_timer.GetElapsedMS() - m_ping_histroy[ping_id]);
					printf("Ping ID: %d, ping: %d \n", ping_id, ping);
					m_ping_histroy.erase(ping_id);
				}
				else {
					printf("INVALID Ping ID: %d !!! \n", ping_id);
				}
			}
			break;
		default:
			break;
	}
}

bool GameClientConnection::AttemptLoadGame(string filename, uint32_t crc32Hash)
{
	if(filename.size() > 0) {
		HashInfo hashInfo;
		hashInfo.Crc32 = crc32Hash;
		if(_console->LoadMatchingRom(filename, hashInfo)) {
			return true;
		} else {
			MessageManager::DisplayMessage("NetPlay", "CouldNotFindRom");
			return false;
		}
	}
	return false;
}

void GameClientConnection::PushControllerState(uint8_t port, ControlDeviceState state)
{
	LockHandler lock = _writeLock.AcquireSafe();
	_inputData[port].push_back(state);
	_inputSize[port]++;

	if(_inputData[port].size() >= _minimumQueueSize) {
		_waitForInput[port].Signal();
	}
}

void GameClientConnection::DisableControllers()
{
	//Used to prevent deadlocks when client is trying to fill its buffer while the host changes the current game/settings/etc. (i.e situations where we need to call Console::Pause())
	ClearInputData();
	_enableControllers = false;
	for(int i = 0; i < BaseControlDevice::PortCount; i++) {
		_waitForInput[i].Signal();
	}
}

bool GameClientConnection::SetInput(BaseControlDevice *device)
{
	if(_enableControllers) {
		uint8_t port = device->GetPort();
		while(_inputSize[port] == 0) {
			_waitForInput[port].Wait();

			if(port == 0 && _minimumQueueSize < 10) {
				//Increase buffer size - reduces freezes at the cost of additional lag
				_minimumQueueSize++;
			}

			if(_shutdown || !_enableControllers) {
				return true;
			}
		}

		LockHandler lock = _writeLock.AcquireSafe();
		ControlDeviceState state = _inputData[port].front();
		_inputData[port].pop_front();
		_inputSize[port]--;

		if(_inputData[port].size() > _minimumQueueSize) {
			//Too much data, catch up
			_console->GetSettings()->SetFlags(EmulationFlags::ForceMaxSpeed);
		} else {
			_console->GetSettings()->ClearFlags(EmulationFlags::ForceMaxSpeed);
			_console->GetSettings()->SetEmulationSpeed(100);
		}

		device->SetRawState(state);
	}
	return true;
}

void GameClientConnection::InitControlDevice()
{
	if(_controllerPort == BaseControlDevice::ExpDevicePort) {
		_newControlDevice = ControlManager::CreateExpansionDevice(_console->GetSettings()->GetExpansionDevice(), _console);
	} else {
		//Pretend we are using port 0 (to use player 1's keybindings during netplay)
		_newControlDevice = ControlManager::CreateControllerDevice(_console->GetSettings()->GetControllerType(_controllerPort), 0, _console);
	}
}

void GameClientConnection::ProcessNotification(ConsoleNotificationType type, void* parameter)
{
	if(type == ConsoleNotificationType::ConfigChanged) {
		InitControlDevice();
	} else if(type == ConsoleNotificationType::GameLoaded) {
		_console->GetControlManager()->RegisterInputProvider(this);
	}
}

void GameClientConnection::SendInput()
{
	if(_gameLoaded) {
		if(_newControlDevice) {
			_controlDevice = _newControlDevice;
			_newControlDevice.reset();
		}

		ControlDeviceState inputState;
		if(_controlDevice) {
			// get input from local device
			_controlDevice->SetStateFromInput();
			inputState = _controlDevice->GetRawState();
		}
		
		if(_lastInputSent != inputState) {
			InputDataMessage message(inputState);
			SendNetMessage(message);
			_lastInputSent = inputState;
		}
	}
}

void GameClientConnection::SendInputTurn() 
{
	// 60 FPS, 3 subturn = 1/20s = 50ms for each lockstep frame
	static constexpr int kSubTurnsPerTurn = 3;

	sub_turn_num_++;
	if (sub_turn_num_ != kSubTurnsPerTurn) {
		return;
	}

	// Turn

}


void GameClientConnection::SendPing()
{
	static uint32_t ping_id = 1;

	PingMessage msg(ping_id);
	SendNetMessage(msg);

	m_ping_histroy[ping_id] = static_cast<int>(_timer.GetElapsedMS());

	ping_id++;
}


void GameClientConnection::SelectController(uint8_t port)
{
	SendControllerSelection(port);
}

uint8_t GameClientConnection::GetAvailableControllers()
{
	uint8_t availablePorts = (1 << BaseControlDevice::PortCount) - 1;
	for(PlayerInfo &playerInfo : _playerList) {
		if(playerInfo.ControllerPort < BaseControlDevice::PortCount) {
			availablePorts &= ~(1 << playerInfo.ControllerPort);
		}
	}
	return availablePorts;
}

uint8_t GameClientConnection::GetControllerPort()
{
	return _controllerPort;
}