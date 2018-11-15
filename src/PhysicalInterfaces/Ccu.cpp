/* Copyright 2013-2017 Sathya Laufer
 *
 * Homegear is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Homegear is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Homegear.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */

#include "Ccu.h"
#include "../GD.h"
#include "../MyPacket.h"

namespace MyFamily
{

Ccu::Ccu(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings) : IPhysicalInterface(GD::bl, GD::family->getFamily(), settings)
{
    if(settings->listenThreadPriority == -1)
    {
        settings->listenThreadPriority = 0;
        settings->listenThreadPolicy = SCHED_OTHER;
    }

    _xmlrpcDecoder.reset(new BaseLib::Rpc::XmlrpcDecoder(GD::bl));
    _xmlrpcEncoder.reset(new BaseLib::Rpc::XmlrpcEncoder(GD::bl));

    _bidcosDevicesExist = false;
    _hmipNewDevicesCalled = false;
    _wiredNewDevicesCalled = false;
    _unreachable = false;
    _forceReInit = false;
    _stopPingThread = false;
    _bidcosReInit = false;
    _hmipReInit = false;
    _wiredReInit = false;

    _out.init(GD::bl);
    BaseLib::HelperFunctions::toUpper(settings->id);
    _out.setPrefix(GD::out.getPrefix() + settings->id + ": ");

    signal(SIGPIPE, SIG_IGN);

    if(!settings)
    {
        _out.printCritical("Critical: Error initializing. Settings pointer is empty.");
        return;
    }

    _stopped = true;
    _lastPongBidcos.store(0);
    _lastPongHmip.store(0);
    _lastPongWired.store(0);

    if(settings->host.empty()) _noHost = true;
    _hostname = settings->host;
    _port = BaseLib::Math::getNumber(settings->port);
    if(_port < 1 || _port > 65535) _port = 2001;
    _port2 = BaseLib::Math::getNumber(settings->port2);
    if((_port2 < 1 || _port2 > 65535) && _port2 != 0) _port2 = 2010;
    _port3 = BaseLib::Math::getNumber(settings->port3);
    if((_port3 < 1 || _port3 > 65535) && _port3 != 0) _port3 = 2000;

    _httpClient = std::unique_ptr<BaseLib::HttpClient>(new HttpClient(_bl, _hostname, 8181, false, false));
}

Ccu::~Ccu()
{
    _stopCallbackThread = true;
    _stopped = true;
    _stopPingThread = true;
    _bl->threadManager.join(_listenThread);
    _bl->threadManager.join(_listenThread2);
    _bl->threadManager.join(_listenThread3);
    GD::bl->threadManager.join(_initThread);
    GD::bl->threadManager.join(_pingThread);
}

void Ccu::reconnect(RpcType rpcType, bool forceReinit)
{
    try
    {
        std::lock_guard<std::mutex> reconnectGuard(_reconnectMutex);
        if(rpcType == RpcType::bidcos) _out.printWarning("Warning: Reconnecting HomeMatic BidCoS...");
        else if(rpcType == RpcType::wired) _out.printWarning("Warning: Reconnecting HomeMatic Wired...");
        else if(rpcType == RpcType::hmip) _out.printWarning("Warning: Reconnecting HomeMatic IP...");

        if(rpcType == RpcType::bidcos) _bidcosClient->close();
        else if(rpcType == RpcType::wired) _wiredClient->close();
        else if(rpcType == RpcType::hmip) _hmipClient->close();

        if(!regaReady())
        {
            GD::out.printInfo("Info: ReGa is not ready (" + std::to_string((int32_t)rpcType) + "). Waiting for 10 seconds...");
            int32_t i = 1;
            while(!_stopped && !_stopCallbackThread)
            {
                if(i % 10 == 0)
                {
                    _lastPongBidcos.store(BaseLib::HelperFunctions::getTime());
                    _lastPongWired.store(BaseLib::HelperFunctions::getTime());
                    _lastPongHmip.store(BaseLib::HelperFunctions::getTime());
                    if(regaReady()) break;
                    GD::out.printInfo("Info: ReGa is not ready (" + std::to_string((int32_t)rpcType) + "). Waiting for 10 seconds...");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                i++;
                continue;
            }
        }

        if(rpcType == RpcType::bidcos)
        {
            _bidcosClient->open();
            _bidcosReInit = true;
        }
        else if(rpcType == RpcType::wired)
        {
            _wiredClient->open();
            _wiredReInit = true;
        }
        else if(rpcType == RpcType::hmip)
        {
            _hmipClient->open();
            _hmipReInit = true;
        }

        if(forceReinit) _forceReInit = true;
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Ccu::init()
{
    try
    {
        if(!regaReady())
        {
            GD::out.printInfo("Info: ReGa is not ready. Waiting for 10 seconds...");
            int32_t i = 1;
            while(!_stopped && !_stopCallbackThread)
            {
                if(i % 10 == 0)
                {
                    _lastPongBidcos.store(BaseLib::HelperFunctions::getTime());
                    _lastPongWired.store(BaseLib::HelperFunctions::getTime());
                    _lastPongHmip.store(BaseLib::HelperFunctions::getTime());
                    if(regaReady()) break;
                    GD::out.printInfo("Info: ReGa is not ready. Waiting for 10 seconds...");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                i++;
                continue;
            }
        }

        _bidcosDevicesExist = false;
        _hmipNewDevicesCalled = false;
        _wiredNewDevicesCalled = false;
        _lastPongBidcos.store(BaseLib::HelperFunctions::getTime());
        _lastPongHmip.store(BaseLib::HelperFunctions::getTime());
        _lastPongWired.store(BaseLib::HelperFunctions::getTime());

        if(_bidcosClient)
        {
            try
            {
                if(!_bidcosClient->connected())
                {
                    _bidcosReInit = true;
                    reconnect(RpcType::bidcos, false);
                }
                else
                {
                    auto parameters = std::make_shared<BaseLib::Array>();
                    parameters->reserve(2);
                    parameters->push_back(std::make_shared<BaseLib::Variable>("http://" + _listenIp + ":" + std::to_string(_listenPort)));
                    parameters->push_back(std::make_shared<BaseLib::Variable>(_bidcosIdString));
                    auto result = invoke(RpcType::bidcos, "init", parameters);
                    if(result->errorStruct)
                    {
                        _out.printError("Error calling \"init\" for HomeMatic BidCoS: " + result->structValue->at("faultString")->stringValue);
                        _bidcosReInit = true;
                        reconnect(RpcType::bidcos, false);
                    }
                    else _bidcosReInit = false;
                }
            }
            catch(const std::exception& ex)
            {
                _bidcosReInit = true;
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
            catch(BaseLib::Exception& ex)
            {
                _bidcosReInit = true;
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
            catch(...)
            {
                _bidcosReInit = true;
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
            }
        }

        if(_hmipClient)
        {
            try
            {
                if(!_hmipClient->connected())
                {
                    _hmipReInit = true;
                    reconnect(RpcType::hmip, false);
                }
                else
                {
                    auto parameters = std::make_shared<BaseLib::Array>();
                    parameters->reserve(2);
                    parameters->push_back(std::make_shared<BaseLib::Variable>("http://" + _listenIp + ":" + std::to_string(_listenPort)));
                    parameters->push_back(std::make_shared<BaseLib::Variable>(_hmipIdString));
                    auto result = invoke(RpcType::hmip, "init", parameters);
                    if(result->errorStruct)
                    {
                        _out.printError("Error calling \"init\" for HomeMatic IP: " + result->structValue->at("faultString")->stringValue);
                        _hmipReInit = true;
                        reconnect(RpcType::hmip, false);
                    }
                    else _hmipReInit = false;
                }
            }
            catch(const std::exception& ex)
            {
                _hmipReInit = true;
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
            catch(BaseLib::Exception& ex)
            {
                _hmipReInit = true;
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
            catch(...)
            {
                _hmipReInit = true;
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
            }
        }

        if(_wiredClient)
        {
            try
            {
                if(!_wiredClient->connected())
                {
                    _wiredReInit = true;
                    reconnect(RpcType::wired, false);
                }
                else
                {
                    auto parameters = std::make_shared<BaseLib::Array>();
                    parameters->reserve(2);
                    parameters->push_back(std::make_shared<BaseLib::Variable>("http://" + _listenIp + ":" + std::to_string(_listenPort)));
                    parameters->push_back(std::make_shared<BaseLib::Variable>(_wiredIdString));
                    auto result = invoke(RpcType::wired, "init", parameters);
                    if(result->errorStruct)
                    {
                        _out.printError("Error calling \"init\" for HomeMatic Wired: " + result->structValue->at("faultString")->stringValue);
                        _wiredReInit = true;
                        reconnect(RpcType::wired, false);
                    }
                    else _wiredReInit = false;
                }
            }
            catch(const std::exception& ex)
            {
                _wiredReInit = true;
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
            catch(BaseLib::Exception& ex)
            {
                _wiredReInit = true;
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
            catch(...)
            {
                _wiredReInit = true;
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
            }
        }

        if(!_bidcosReInit && !_hmipReInit && !_wiredReInit) _out.printInfo("Info: Init complete.");
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Ccu::deinit()
{
    try
    {
        BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
        parameters->reserve(2);
        parameters->push_back(std::make_shared<BaseLib::Variable>("http://" + _listenIp + ":" + std::to_string(_listenPort)));
        parameters->push_back(std::make_shared<BaseLib::Variable>(std::string("")));
        if(_bidcosClient && _bidcosClient->connected())
        {
            auto result = invoke(RpcType::bidcos, "init", parameters, false);
            if(result->errorStruct) _out.printError("Error calling (de-)\"init\" for HomeMatic BidCoS: " + result->structValue->at("faultString")->stringValue);
        }

        if(_hmipClient && _hmipClient->connected())
        {
            parameters->at(0)->stringValue = "http://" + _listenIp + ":" + std::to_string(_listenPort);
            parameters->at(1)->stringValue = "";
            auto result = invoke(RpcType::hmip, "init", parameters, false);
            if(result->errorStruct) _out.printError("Error calling (de-)\"init\" for HomeMatic IP: " + result->structValue->at("faultString")->stringValue);
        }

        if(_wiredClient && _wiredClient->connected())
        {
            parameters->at(0)->stringValue = "http://" + _listenIp + ":" + std::to_string(_listenPort);
            parameters->at(1)->stringValue = "";
            auto result = invoke(RpcType::wired, "init", parameters, false);
            if(result->errorStruct) _out.printError("Error calling (de-)\"init\" for HomeMatic Wired: " + result->structValue->at("faultString")->stringValue);
        }

        _out.printInfo("Info: Deinit complete.");
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Ccu::startListening()
{
    try
    {
        stopListening();

        _noHost = _settings->host.empty();

        if(!_noHost)
        {
            _stopped = false;
            _lastPongBidcos.store(BaseLib::HelperFunctions::getTime());
            _lastPongHmip.store(BaseLib::HelperFunctions::getTime());
            _lastPongWired.store(BaseLib::HelperFunctions::getTime());
            _bidcosDevicesExist = false;
            _hmipNewDevicesCalled = false;
            _wiredNewDevicesCalled = false;
            _bidcosReInit = false;
            _hmipReInit = false;
            _wiredReInit = false;

            BaseLib::TcpSocket::TcpServerInfo serverInfo;
            serverInfo.newConnectionCallback = std::bind(&Ccu::newConnection, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
            serverInfo.connectionClosedCallback = std::bind(&Ccu::connectionClosed, this, std::placeholders::_1);
            serverInfo.packetReceivedCallback = std::bind(&Ccu::packetReceived, this, std::placeholders::_1, std::placeholders::_2);

            std::string settingName = "eventServerPortRange";
            auto setting = GD::family->getFamilySetting(settingName);
            int32_t portRangeStart = 9000;
            int32_t portRangeEnd = 9100;
            if(setting)
            {
                std::string portRangeString = setting->stringValue;
                auto portRangePair = BaseLib::HelperFunctions::splitFirst(portRangeString, '-');
                BaseLib::HelperFunctions::trim(portRangePair.first);
                BaseLib::HelperFunctions::trim(portRangePair.second);
                portRangeStart = BaseLib::Math::getNumber(portRangePair.first);
                portRangeEnd = BaseLib::Math::getNumber(portRangePair.second);
                if(portRangeStart < 1024 || portRangeStart > 65535) portRangeStart = 9000;
                if(portRangeEnd < 1024 || portRangeEnd > 65535) portRangeEnd = 9100;
            }

            _server = std::make_shared<BaseLib::TcpSocket>(GD::bl, serverInfo);
            std::string listenAddress;
            bool serverBound = false;
            for(int32_t i = portRangeStart; i <= portRangeEnd; i++)
            {
                try
                {
                    _server->startServer("0.0.0.0", std::to_string(i), listenAddress);
                    _out.printInfo("RPC server started listening on " + listenAddress + ":" + std::to_string(i));
                    _listenPort = i;
                    serverBound = true;
                    break;
                }
                catch(BaseLib::SocketAddressInUseException& ex)
                {
                    continue;
                }
            }
            if(!serverBound)
            {
                _stopped = true;
                _noHost = true;
                return;
            }

            settingName = "eventServerIp";
            setting = GD::family->getFamilySetting(settingName);
            if(setting) _listenIp = setting->stringValue;
            if(!BaseLib::Net::isIp(_listenIp)) _listenIp = BaseLib::Net::getMyIpAddress(_listenIp);
            _out.printInfo("Info: My own IP address is " + _listenIp + ".");

            _out.printInfo("Info: Connecting to IP " + _hostname + " and ports " + (_port != 0 ? std::to_string(_port) : "") + (_port3 != 0 ? ", " + std::to_string(_port3) : "") + (_port2 != 0 ? ", " + std::to_string(_port2) : "") + ".");

            if(_port != 0)
            {
                try
                {
                    _bidcosClient = std::unique_ptr<BaseLib::TcpSocket>(new BaseLib::TcpSocket(_bl, _hostname, std::to_string(_port)));
                    _bidcosClient->open();
                }
                catch(BaseLib::Exception& ex)
                {
                    _out.printError("Could not connect to HomeMatic BidCoS port. Assuming HomeMatic BidCoS is not available.");
                    _bidcosClient.reset();
                }
            }
            if(_port2 != 0)
            {
                try
                {
                    _hmipClient = std::unique_ptr<BaseLib::TcpSocket>(new BaseLib::TcpSocket(_bl, _hostname, std::to_string(_port2)));
                    _hmipClient->open();
                }
                catch(BaseLib::Exception& ex)
                {
                    _out.printWarning("Could not connect to HomeMatic IP port. Assuming HomeMatic IP is not available.");
                    _hmipClient.reset();
                }
            }
            if(_port3 != 0)
            {
                try
                {
                    _wiredClient = std::unique_ptr<BaseLib::TcpSocket>(new BaseLib::TcpSocket(_bl, _hostname, std::to_string(_port3)));
                    _wiredClient->open();
                }
                catch(BaseLib::Exception& ex)
                {
                    _out.printWarning("Could not connect to HomeMatic Wired port. Assuming HomeMatic Wired is not available.");
                    _wiredClient.reset();
                }
            }
            _ipAddress = "";
            if(_bidcosClient) _ipAddress = _bidcosClient->getIpAddress();
            else if(_hmipClient) _ipAddress = _hmipClient->getIpAddress();
            else if(_wiredClient) _ipAddress = _wiredClient->getIpAddress();
            _noHost = _hostname.empty();

            _bidcosIdString = "Homegear_BidCoS_" + _listenIp + "_" + std::to_string(_listenPort);
            _hmipIdString = "Homegear_HMIP_" + _listenIp + "_" + std::to_string(_listenPort);
            _wiredIdString = "Homegear_Wired_" + _listenIp + "_" + std::to_string(_listenPort);

            if(_bidcosClient && _port != 0)
            {
                if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &Ccu::listen, this, RpcType::bidcos);
                else _bl->threadManager.start(_listenThread, true, &Ccu::listen, this, RpcType::bidcos);
            }

            if(_hmipClient && _port2 != 0)
            {
                if(!_bidcosClient) _connectedRpcType = RpcType::hmip;

                if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread2, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &Ccu::listen, this, RpcType::hmip);
                else _bl->threadManager.start(_listenThread2, true, &Ccu::listen, this, RpcType::hmip);
            }

            if(_wiredClient && _port3 != 0)
            {
                if(!_bidcosClient && _connectedRpcType == RpcType::bidcos) _connectedRpcType = RpcType::wired;

                if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread3, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &Ccu::listen, this, RpcType::wired);
                else _bl->threadManager.start(_listenThread3, true, &Ccu::listen, this, RpcType::wired);
            }

            _stopPingThread = false;
            _bl->threadManager.start(_pingThread, true, &Ccu::ping, this);
        }
        IPhysicalInterface::startListening();
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Ccu::stopListening()
{
    try
    {
        _stopPingThread = true;

        deinit();

        _stopped = true;
        _stopCallbackThread = true;
        _bl->threadManager.join(_listenThread);
        _stopCallbackThread = false;

        _bl->threadManager.join(_pingThread);

        if(_bidcosClient) _bidcosClient->close();
        if(_hmipClient) _hmipClient->close();
        if(_wiredClient) _wiredClient->close();
        if(_server)
        {
            _server->stopServer();
            _server->waitForServerStopped();
        }
        IPhysicalInterface::stopListening();
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Ccu::newConnection(int32_t clientId, std::string address, uint16_t port)
{
    try
    {
        if(GD::bl->debugLevel >= 5) _out.printDebug("Debug: New connection from " + address + " on port " + std::to_string(port) + ". Client ID is: " + std::to_string(clientId));
        CcuClientInfo clientInfo;
        clientInfo.http = std::make_shared<BaseLib::Http>();

        std::lock_guard<std::mutex> ccuClientInfoGuard(_ccuClientInfoMutex);
        _ccuClientInfo[clientId] = std::move(clientInfo);
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Ccu::connectionClosed(int32_t clientId)
{
    try
    {
        if(GD::bl->debugLevel >= 5) _out.printDebug("Debug: Connection to client " + std::to_string(clientId) + " closed.");

        std::lock_guard<std::mutex> ccuClientInfoGuard(_ccuClientInfoMutex);
        _ccuClientInfo.erase(clientId);
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Ccu::packetReceived(int32_t clientId, BaseLib::TcpSocket::TcpPacket packet)
{
    std::shared_ptr<BaseLib::Http> http;

    try
    {
        if(GD::bl->debugLevel >= 5) _out.printDebug("Debug: Raw packet " + BaseLib::HelperFunctions::getHexString(packet));

        {
            std::lock_guard<std::mutex> ccuClientInfoGuard(_ccuClientInfoMutex);
            auto clientIterator = _ccuClientInfo.find(clientId);
            if(clientIterator == _ccuClientInfo.end())
            {
                _out.printError("Error: Client with ID " + std::to_string(clientId) + " not found in map.");
                return;
            }
            http = clientIterator->second.http;
        }

        if(packet.empty()) return;
        uint32_t processedBytes = 0;
        try
        {
            while(processedBytes < packet.size())
            {
                std::string methodName;
                BaseLib::PArray parameters;

                processedBytes += http->process((char*)packet.data() + processedBytes, packet.size() - processedBytes);
                if(http->isFinished())
                {
                    if(http->getHeader().method == "POST")
                    {
                        parameters = _xmlrpcDecoder->decodeRequest(http->getContent(), methodName);
                        processPacket(clientId, methodName, parameters);
                    }
                    http->reset();
                }
            }
        }
        catch(BaseLib::Rpc::BinaryRpcException& ex)
        {
            _out.printError("Error processing packet (1): " + ex.what());
        }
        return;
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what() + " Packet was: " + BaseLib::HelperFunctions::getHexString(packet));
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }

    http->reset();
}

void Ccu::processPacket(int32_t clientId, std::string& methodName, BaseLib::PArray parameters)
{
    try
    {
        BaseLib::PVariable response = std::make_shared<BaseLib::Variable>();

        if(!parameters->empty() && parameters->at(0)->stringValue == _hmipIdString) _lastPongHmip.store(BaseLib::HelperFunctions::getTime());

        if(methodName == "system.multicall")
        {
            if(!parameters->empty())
            {
                response->setType(BaseLib::VariableType::tArray);
                response->arrayValue->reserve(parameters->at(0)->arrayValue->size());
                for(auto& methodEntry : *parameters->at(0)->arrayValue)
                {
                    response->arrayValue->push_back(std::make_shared<BaseLib::Variable>());

                    auto methodNameIterator = methodEntry->structValue->find("methodName");
                    if(methodNameIterator == methodEntry->structValue->end()) continue;
                    auto parametersIterator = methodEntry->structValue->find("params");
                    PArray parameters = parametersIterator == methodEntry->structValue->end() ? std::make_shared<BaseLib::Array>() : parametersIterator->second->arrayValue;

                    if(methodNameIterator->second->stringValue == "event" && parameters->size() == 4 && parameters->at(2)->stringValue == "PONG")
                    {
                        if(_bl->debugLevel >= 5) _out.printDebug("Debug: Pong received. ID: " + parameters->at(0)->stringValue);
                        if(parameters->at(0)->stringValue == _bidcosIdString)
                        {
                            _lastPongBidcos.store(BaseLib::HelperFunctions::getTime());
                            if(_bl->debugLevel >= 5) _out.printDebug("Debug: BidCoS pong received. Stored time: " + std::to_string(_lastPongBidcos.load()));
                        }
                        else if(parameters->at(0)->stringValue == _wiredIdString) _lastPongWired.store(BaseLib::HelperFunctions::getTime());
                    }
                    else
                    {
                        if(!parameters->empty())
                        {
                            if(parameters->at(0)->stringValue == _bidcosIdString) parameters->at(0)->integerValue = (int32_t) RpcType::bidcos;
                            else if(parameters->at(0)->stringValue == _hmipIdString) parameters->at(0)->integerValue = (int32_t) RpcType::hmip;
                            else if(parameters->at(0)->stringValue == _wiredIdString) parameters->at(0)->integerValue = (int32_t) RpcType::wired;
                            _out.printInfo("Info: CCU (" + std::to_string(parameters->at(0)->integerValue) + ") is calling RPC method " + methodNameIterator->second->stringValue);
                            PMyPacket packet = std::make_shared<MyPacket>(methodNameIterator->second->stringValue, parameters);
                            raisePacketReceived(packet);
                        }
                    }
                }
            }
        }
        else if(methodName == "newDevices")
        {
            if(parameters->at(0)->stringValue == _bidcosIdString)
            {
                parameters->at(0)->integerValue = (int32_t) RpcType::bidcos;
                _bidcosDevicesExist = parameters->at(1)->arrayValue->size() > 52;
            }
            else if(parameters->at(0)->stringValue == _wiredIdString)
            {
                parameters->at(0)->integerValue = (int32_t)RpcType::wired;
                _wiredNewDevicesCalled = true;
            }
            else
            {
                parameters->at(0)->integerValue = (int32_t) RpcType::hmip;
                _hmipNewDevicesCalled = true;
            }
            _out.printInfo("Info: CCU (" + std::to_string(parameters->at(0)->integerValue) + ") is calling RPC method " + methodName);
            PMyPacket packet = std::make_shared<MyPacket>(methodName, parameters);
            raisePacketReceived(packet);
        }
        else if(methodName == "system.listMethods" || methodName == "listDevices")
        {
            if(parameters->at(0)->stringValue == _bidcosIdString) parameters->at(0)->integerValue = (int32_t) RpcType::bidcos;
            else if(parameters->at(0)->stringValue == _wiredIdString) parameters->at(0)->integerValue = (int32_t)RpcType::wired;
            else parameters->at(0)->integerValue = (int32_t)RpcType::hmip;
            _out.printInfo("Info: CCU (" + std::to_string(parameters->at(0)->integerValue) + ") is calling RPC method " + methodName);
            response->setType(BaseLib::VariableType::tArray);
        }
        else
        {
            if(parameters && !parameters->empty())
            {
                if(parameters->at(0)->stringValue == _bidcosIdString) parameters->at(0)->integerValue = (int32_t) RpcType::bidcos;
                else if(parameters->at(0)->stringValue == _hmipIdString) parameters->at(0)->integerValue = (int32_t) RpcType::hmip;
                else if(parameters->at(0)->stringValue == _wiredIdString) parameters->at(0)->integerValue = (int32_t) RpcType::wired;
                _out.printInfo("Info: CCU (" + std::to_string(parameters->at(0)->integerValue) + ") is calling RPC method " + methodName);
                PMyPacket packet = std::make_shared<MyPacket>(methodName, parameters);
                raisePacketReceived(packet);
            }
        }

        BaseLib::TcpSocket::TcpPacket responsePacket;
        std::vector<uint8_t> xmlData;
        _xmlrpcEncoder->encodeResponse(response, xmlData);
        std::string header = "HTTP/1.1 200 OK\r\nConnection: Close\r\nContent-Type: text/xml\r\nContent-Length: " + std::to_string(xmlData.size()) + "\r\n\r\n";
        responsePacket.reserve(header.size() + xmlData.size());
        responsePacket.insert(responsePacket.end(), header.begin(), header.end());
        responsePacket.insert(responsePacket.end(), xmlData.begin(), xmlData.end());
        _server->sendToClient(clientId, responsePacket, true);
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Ccu::listen(Ccu::RpcType rpcType)
{
    try
    {
        uint32_t bytesRead = 0;
        uint32_t processedBytes = 0;
        std::vector<char> buffer(1024);
        BaseLib::Rpc::BinaryRpc binaryRpc(GD::bl);
        BaseLib::Http http;

        if(rpcType == _connectedRpcType)
        {
            //Only start threads once
            _bl->threadManager.start(_initThread, true, &Ccu::init, this);
        }

        while(!_stopped && !_stopCallbackThread)
        {
            try
            {
                try
                {
                    if(rpcType == RpcType::bidcos) bytesRead = _bidcosClient->proofread(buffer.data(), buffer.size());
                    else if(rpcType == RpcType::wired) bytesRead = _wiredClient->proofread(buffer.data(), buffer.size());
                    else if(rpcType == RpcType::hmip) bytesRead = _hmipClient->proofread(buffer.data(), buffer.size());
                }
                catch(SocketTimeOutException& ex)
                {
                    if(ex.getType() == SocketTimeOutException::SocketTimeOutType::readTimeout)
                    {
                        if(_bidcosReInit || _wiredReInit || _hmipReInit) continue;
                        reconnect(rpcType, true);
                    }
                    continue;
                }

                if(bytesRead > buffer.size()) bytesRead = buffer.size();

                try
                {
                    processedBytes = 0;
                    while(processedBytes < bytesRead)
                    {
                        processedBytes += http.process(buffer.data() + processedBytes, bytesRead - processedBytes, true);
                        if(http.isFinished())
                        {
                            std::unique_lock<std::mutex> waitLock(_requestWaitMutex);
                            {
                                std::lock_guard<std::mutex> responseGuard(_responseMutex);
                                _response = _xmlrpcDecoder->decodeResponse(http.getContent());
                            }
                            waitLock.unlock();
                            _requestConditionVariable.notify_all();
                            http.reset();
                        }
                    }
                }
                catch(BaseLib::Rpc::BinaryRpcException& ex)
                {
                    _out.printError("Error processing packet (2): " + ex.what());
                    http.reset();
                    binaryRpc.reset();
                }
            }
            catch(const std::exception& ex)
            {
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                reconnect(rpcType, true);
            }
            catch(BaseLib::Exception& ex)
            {
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                reconnect(rpcType, true);
            }
            catch(...)
            {
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                reconnect(rpcType, true);
            }
        }
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

void Ccu::ping()
{
    try
    {
        while(!_stopped && !_stopCallbackThread && !_stopPingThread)
        {
            for(int32_t i = 0; i < 30; i++)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                if(_stopped || _stopCallbackThread || _stopPingThread) return;
                if(_forceReInit)
                {
                    _forceReInit = false;
                    break;
                }
            }

            if(!isOpen())
            {
                auto data = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
                data->structValue->emplace("IP_ADDRESS", std::make_shared<BaseLib::Variable>(_ipAddress));
                data->structValue->emplace("SERIALNUMBER", std::make_shared<BaseLib::Variable>(_settings->serialNumber));
                if(!_unreachable)
                {
                    _unreachable = true;
                    _bl->globalServiceMessages.set(MY_FAMILY_ID, 0, _settings->id, BaseLib::HelperFunctions::getTimeSeconds(), "l10n.ccu.serviceMessage.ccuUnreachable", std::list<std::string>{ _settings->serialNumber, _ipAddress }, data, 1);
                }
            }
            else
            {
                _unreachable = false;
                _bl->globalServiceMessages.unset(MY_FAMILY_ID, 0, _settings->id, "l10n.ccu.serviceMessage.ccuUnreachable");

                getCcuServiceMessages();
            }

            if(_bidcosClient && _bidcosDevicesExist)
            {
                BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
                parameters->push_back(std::make_shared<BaseLib::Variable>(_bidcosIdString));
                auto result = invoke(RpcType::bidcos, "ping", parameters);
                if(result->errorStruct)
                {
                    _out.printError("Error calling \"ping\" (BidCoS): " + result->structValue->at("faultString")->stringValue);
                    _bidcosReInit = true;
                    _bidcosClient->close();
                }
            }

            if(_bidcosClient && ((_bidcosDevicesExist && BaseLib::HelperFunctions::getTime() - _lastPongBidcos.load() > 70000) || _bidcosReInit))
            {
                if(regaReady())
                {
                    if(!_bidcosReInit) _out.printError("Error: No keep alive response received (BidCoS). Last pong: " + std::to_string(_lastPongBidcos.load()) + ". Reinitializing...");
                    init();
                }
                else _bidcosReInit = true;
            }

            if(_wiredClient && ((_wiredNewDevicesCalled && BaseLib::HelperFunctions::getTime() - _lastPongWired.load() > 3600000) || _wiredReInit))
            {
                if(regaReady())
                {
                    if(!_wiredReInit) _out.printError("Error: No keep alive received (Wired). Reinitializing...");
                    init();
                }
                else _wiredReInit = true;
            }

            if(_hmipClient && ((_hmipNewDevicesCalled && BaseLib::HelperFunctions::getTime() - _lastPongHmip.load() > 3600000) || _hmipReInit))
            {
                if(regaReady())
                {
                    if(!_hmipReInit) _out.printError("Error: No keep alive received (HM-IP). Reinitializing...");
                    init();
                }
                else _hmipReInit = true;
            }

            if(_port != 0 && !_bidcosClient)
            {
                try
                {
                    _bidcosClient = std::unique_ptr<BaseLib::TcpSocket>(new BaseLib::TcpSocket(_bl, _hostname, std::to_string(_port2)));
                    _bidcosClient->open();
                }
                catch(BaseLib::Exception& ex)
                {
                    _bidcosClient.reset();
                }

                if(_bidcosClient)
                {
                    if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread2, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &Ccu::listen, this, RpcType::bidcos);
                    else _bl->threadManager.start(_listenThread2, true, &Ccu::listen, this, RpcType::bidcos);
                }
            }

            if(_port2 != 0 && !_hmipClient)
            {
                try
                {
                    _hmipClient = std::unique_ptr<BaseLib::TcpSocket>(new BaseLib::TcpSocket(_bl, _hostname, std::to_string(_port2)));
                    _hmipClient->open();
                }
                catch(BaseLib::Exception& ex)
                {
                    _hmipClient.reset();
                }

                if(_hmipClient)
                {
                    if(!_bidcosClient) _connectedRpcType = RpcType::hmip;

                    if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread2, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &Ccu::listen, this, RpcType::hmip);
                    else _bl->threadManager.start(_listenThread2, true, &Ccu::listen, this, RpcType::hmip);
                }
            }

            if(_port3 != 0 && !_wiredClient)
            {
                try
                {
                    _wiredClient = std::unique_ptr<BaseLib::TcpSocket>(new BaseLib::TcpSocket(_bl, _hostname, std::to_string(_port3)));
                    _wiredClient->open();
                }
                catch(BaseLib::Exception& ex)
                {
                    _wiredClient.reset();
                }

                if(_wiredClient)
                {
                    if(!_bidcosClient && _connectedRpcType == RpcType::bidcos) _connectedRpcType = RpcType::wired;

                    if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread3, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &Ccu::listen, this, RpcType::wired);
                    else _bl->threadManager.start(_listenThread3, true, &Ccu::listen, this, RpcType::wired);
                }
            }

            if(_ipAddress.empty())
            {
                if(_bidcosClient) _ipAddress = _bidcosClient->getIpAddress();
                else if(_hmipClient) _ipAddress = _hmipClient->getIpAddress();
                else if(_wiredClient) _ipAddress = _wiredClient->getIpAddress();
                _noHost = _hostname.empty();
            }
        }
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

BaseLib::PVariable Ccu::invoke(Ccu::RpcType rpcType, std::string methodName, BaseLib::PArray parameters, bool wait)
{
    try
    {
        if(_stopped) return BaseLib::Variable::createError(-32500, "CCU is stopped.");
        if(rpcType == RpcType::bidcos && !_bidcosClient) return BaseLib::Variable::createError(-32501, "HomeMatic BidCoS is disabled.");
        else if(rpcType == RpcType::hmip && !_hmipClient) return BaseLib::Variable::createError(-32501, "HomeMatic IP is disabled.");
        else if(rpcType == RpcType::wired && !_wiredClient) return BaseLib::Variable::createError(-32501, "HomeMatic Wired is disabled.");

        std::lock_guard<std::mutex> invokeGuard(_invokeMutex);

        std::vector<char> data;
        std::vector<char> xmlData;
        _xmlrpcEncoder->encodeRequest(methodName, parameters, xmlData);
        xmlData.push_back('\r');
        xmlData.push_back('\n');
        std::string header = "POST / HTTP/1.1\r\nUser-Agent: homegear-ccu\r\nHost: " + _ipAddress + ":" + std::to_string(_port2) + "\r\nContent-Type: text/xml\r\nContent-Length: " + std::to_string(xmlData.size()) + "\r\nConnection: Keep-Alive\r\n\r\n";
        data.reserve(header.size() + xmlData.size());
        data.insert(data.end(), header.begin(), header.end());
        data.insert(data.end(), xmlData.begin(), xmlData.end());

        if(wait)
        {
            std::lock_guard<std::mutex> responseGuard(_responseMutex);
            _response = std::make_shared<BaseLib::Variable>();
        }

        try
        {
            if(rpcType == RpcType::bidcos) _bidcosClient->proofwrite(data);
            else if(rpcType == RpcType::hmip) _hmipClient->proofwrite(data);
            else if(rpcType == RpcType::wired) _wiredClient->proofwrite(data);
        }
        catch(SocketOperationException& ex)
        {
            _out.printError("Error: Could not write to socket: " + ex.what());
            return BaseLib::Variable::createError(-1, ex.what());
        }

        if(wait)
        {
            std::unique_lock<std::mutex> waitLock(_requestWaitMutex);
            _requestConditionVariable.wait_for(waitLock, std::chrono::milliseconds(60000), [&]
            {
                std::lock_guard<std::mutex> responseGuard(_responseMutex);
                return _response->type != BaseLib::VariableType::tVoid || _stopped;
            });

            std::lock_guard<std::mutex> responseGuard(_responseMutex);
            if(_response->type == BaseLib::VariableType::tVoid)
            {
                _out.printError("Error: No response received to RPC request. Method: " + methodName);
                return BaseLib::Variable::createError(-1, "No response received.");
            }

            return _response;
        }
        else return std::make_shared<BaseLib::Variable>();
    }
    catch(const std::exception& ex)
    {
        return BaseLib::Variable::createError(-32500, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        return BaseLib::Variable::createError(-32500, ex.what());
    }
    catch(...)
    {
        return BaseLib::Variable::createError(-32500, "Unknown application error.");
    }
}

bool Ccu::regaReady()
{
    try
    {
        HttpClient client(_bl, _hostname, 80, false);
        std::string path = "/ise/checkrega.cgi";
        std::string response;
        try
        {
            client.get(path, response);
        }
        catch(BaseLib::HttpClientException& ex)
        {
            return false;
        }
        if(response == "OK") return true;
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return false;
}

std::unordered_map<std::string, std::string> Ccu::getNames()
{
    std::unordered_map<std::string, std::string> deviceNames;
    try
    {
        BaseLib::Ansi ansi(true, false);
        std::string regaResponse;
        _httpClient->post("/tclrega.exe", _getNamesScript, regaResponse);
        BaseLib::Rpc::JsonDecoder jsonDecoder(_bl);
        auto namesJson = jsonDecoder.decode(regaResponse);
        auto devicesIterator = namesJson->structValue->find("Devices");
        if(devicesIterator != namesJson->structValue->end()) namesJson = devicesIterator->second;
        for(auto& nameElement : *namesJson->arrayValue)
        {
            auto addressIterator = nameElement->structValue->find("Address");
            auto nameIterator = nameElement->structValue->find("Name");
            if(addressIterator == nameElement->structValue->end() || nameIterator == nameElement->structValue->end()) continue;

            nameIterator->second->stringValue = ansi.toUtf8(nameIterator->second->stringValue);
            deviceNames.emplace(addressIterator->second->stringValue, nameIterator->second->stringValue);
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return deviceNames;
}

void Ccu::getCcuServiceMessages()
{
    try
    {
        BaseLib::Ansi ansi(true, false);
        std::string regaResponse;
        _httpClient->post("/tclrega.exe", _getServiceMessagesScript, regaResponse);
        BaseLib::Rpc::JsonDecoder jsonDecoder(_bl);
        auto serviceMessagesJson = jsonDecoder.decode(regaResponse);

        std::lock_guard<std::mutex> serviceMessagesGuard(_serviceMessagesMutex);
        _serviceMessages.clear();

        auto serviceMessagesIterator = serviceMessagesJson->structValue->find("serviceMessages");
        if(serviceMessagesIterator != serviceMessagesJson->structValue->end())
        {
            _serviceMessages.reserve(serviceMessagesIterator->second->arrayValue->size());
            for(auto& serviceMessageElement : *serviceMessagesIterator->second->arrayValue)
            {
                auto addressIterator = serviceMessageElement->structValue->find("address");
                auto stateIterator = serviceMessageElement->structValue->find("state");
                auto messageIterator = serviceMessageElement->structValue->find("message");
                auto timeIterator = serviceMessageElement->structValue->find("time");

                if(addressIterator == serviceMessageElement->structValue->end() || stateIterator == serviceMessageElement->structValue->end() || messageIterator == serviceMessageElement->structValue->end() || timeIterator == serviceMessageElement->structValue->end())
                {
                    continue;
                }

                auto serviceMessage = std::make_shared<CcuServiceMessage>();
                serviceMessage->serial = addressIterator->second->stringValue;
                serviceMessage->value = stateIterator->second->stringValue == "1";
                serviceMessage->message = messageIterator->second->stringValue;
                serviceMessage->time = BaseLib::Math::getNumber(timeIterator->second->stringValue);

                _serviceMessages.emplace_back(std::move(serviceMessage));
            }
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
}

}
