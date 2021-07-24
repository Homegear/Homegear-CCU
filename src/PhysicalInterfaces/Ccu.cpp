/* Copyright 2013-2019 Homegear GmbH
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

    _out.init(GD::bl);
    BaseLib::HelperFunctions::toUpper(settings->id);
    _out.setPrefix(GD::out.getPrefix() + settings->id + ": ");

    signal(SIGPIPE, SIG_IGN);

    if(!settings)
    {
        _out.printCritical("Critical: Error initializing. Settings pointer is empty.");
        return;
    }

    if(settings->host.empty()) _noHost = true;
    _hostname = settings->host;
    _port = BaseLib::Math::getNumber(settings->port);
    if(_port < 1 || _port > 65535) _port = 2001;
    _port2 = BaseLib::Math::getNumber(settings->port2);
    if((_port2 < 1 || _port2 > 65535) && _port2 != 0) _port2 = 2010;
    _port3 = BaseLib::Math::getNumber(settings->port3);
    if((_port3 < 1 || _port3 > 65535) && _port3 != 0) _port3 = 2000;
    _port4 = BaseLib::Math::getNumber(settings->port4);
    if((_port4 < 1 || _port4 > 65535) && _port4 != 0) _port4 = 9292;

    _httpClient = std::unique_ptr<BaseLib::HttpClient>(new HttpClient(_bl, _hostname, 8181, false, false));
}

Ccu::~Ccu()
{
    _stopCallbackThread = true;
    _stopped = true;
    _stopPingThread = true;
    GD::bl->threadManager.join(_initThread);
    GD::bl->threadManager.join(_pingThread);
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
        _hmVirtualNewDevicesCalled = false;
        _lastPongBidcos.store(BaseLib::HelperFunctions::getTime());
        _lastPongHmip.store(BaseLib::HelperFunctions::getTime());
        _lastPongWired.store(BaseLib::HelperFunctions::getTime());
        _lastPongHmVirtual.store(BaseLib::HelperFunctions::getTime());

        if(_bidcosClient)
        {
            try
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
                }
                else _bidcosReInit = false;
            }
            catch(const std::exception& ex)
            {
                _bidcosReInit = true;
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
        }

        if(_hmipClient)
        {
            try
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
                }
                else _hmipReInit = false;
            }
            catch(const std::exception& ex)
            {
                _hmipReInit = true;
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
        }

        if(_wiredClient)
        {
            try
            {
                auto parameters = std::make_shared<BaseLib::Array>();
                parameters->reserve(2);
                parameters->push_back(std::make_shared<BaseLib::Variable>("http://" + _listenIp + ":" + std::to_string(_listenPort)));
                parameters->push_back(std::make_shared<BaseLib::Variable>(_wiredIdString));
                auto result = invoke(RpcType::wired, "init", parameters);
                if(result->errorStruct)
                {
                    if(result->structValue->at("faultCode")->integerValue == 400 || result->structValue->at("faultCode")->integerValue == 503)
                    {
                        _out.printInfo("Info: HomeMatic Wired is not enabled on CCU.");
                        _wiredReInit.store(false, std::memory_order_release);
                        _wiredDisabled.store(true, std::memory_order_release);
                    }
                    else
                    {
                        _out.printError("Error calling \"init\" for HomeMatic Wired (" + std::to_string(result->structValue->at("faultCode")->integerValue64) + "): " + result->structValue->at("faultString")->stringValue);
                        _wiredReInit.store(true, std::memory_order_release);
                    }
                }
                else _wiredReInit.store(false, std::memory_order_release);
            }
            catch(const std::exception& ex)
            {
                _wiredReInit.store(true, std::memory_order_release);
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
        }

        if(_hmVirtualClient)
        {
            try
            {
                auto parameters = std::make_shared<BaseLib::Array>();
                parameters->reserve(2);
                parameters->push_back(std::make_shared<BaseLib::Variable>("http://" + _listenIp + ":" + std::to_string(_listenPort)));
                parameters->push_back(std::make_shared<BaseLib::Variable>(_hmVirtualIdString));
                auto result = invoke(RpcType::hmvirtual, "init", parameters);
                if(result->errorStruct)
                {
                    _out.printError("Error calling \"init\" for HomeMatic Virtual Devices: " + result->structValue->at("faultString")->stringValue);
                    _hmVirtualReInit = true;
                }
                else _hmVirtualReInit = false;
            }
            catch(const std::exception& ex)
            {
                _hmVirtualReInit = true;
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
            }
        }

        if(!_bidcosReInit.load(std::memory_order_acquire) && !_hmipReInit.load(std::memory_order_acquire) && !_wiredReInit.load(std::memory_order_acquire) && !_hmVirtualReInit.load(std::memory_order_acquire)) _out.printInfo("Info: Init complete.");
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
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
        if(_bidcosClient)
        {
            auto result = invoke(RpcType::bidcos, "init", parameters);
            if(result->errorStruct) _out.printError("Error calling (de-)\"init\" for HomeMatic BidCoS: " + result->structValue->at("faultString")->stringValue);
        }

        if(_hmipClient)
        {
            parameters->at(0)->stringValue = "http://" + _listenIp + ":" + std::to_string(_listenPort);
            parameters->at(1)->stringValue = "";
            auto result = invoke(RpcType::hmip, "init", parameters);
            if(result->errorStruct) _out.printError("Error calling (de-)\"init\" for HomeMatic IP: " + result->structValue->at("faultString")->stringValue);
        }

        if(_wiredClient && !_wiredDisabled)
        {
            parameters->at(0)->stringValue = "http://" + _listenIp + ":" + std::to_string(_listenPort);
            parameters->at(1)->stringValue = "";
            auto result = invoke(RpcType::wired, "init", parameters);
            if(result->errorStruct) _out.printError("Error calling (de-)\"init\" for HomeMatic Wired: " + result->structValue->at("faultString")->stringValue);
        }

        if(_hmVirtualClient)
        {
            parameters->at(0)->stringValue = "http://" + _listenIp + ":" + std::to_string(_listenPort);
            parameters->at(1)->stringValue = "";
            auto result = invoke(RpcType::hmvirtual, "init", parameters);
            if(result->errorStruct) _out.printError("Error calling (de-)\"init\" for HomeMatic Virtual Devices: " + result->structValue->at("faultString")->stringValue);
        }

        _out.printInfo("Info: Deinit complete.");
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
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
            _lastPongHmVirtual.store(BaseLib::HelperFunctions::getTime());
            _bidcosDevicesExist = false;
            _hmipNewDevicesCalled = false;
            _wiredNewDevicesCalled = false;
            _hmVirtualNewDevicesCalled = false;
            _bidcosReInit = false;
            _hmipReInit = false;
            _wiredReInit = false;
            _hmVirtualReInit = false;

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

            _out.printInfo("Info: Connecting to IP " + _hostname + " and ports " + (_port != 0 ? std::to_string(_port) : "") + (_port3 != 0 ? ", " + std::to_string(_port3) : "") + (_port2 != 0 ? ", " + std::to_string(_port2) : "") + (_port4 != 0 ? ", " + std::to_string(_port4) : "") + ".");

            if(_port != 0) _bidcosClient = std::unique_ptr<BaseLib::HttpClient>(new BaseLib::HttpClient(_bl, _hostname, _port, false, false));
            if(_port2 != 0) _hmipClient = std::unique_ptr<BaseLib::HttpClient>(new BaseLib::HttpClient(_bl, _hostname, _port2, false, false));
            if(_port3 != 0) _wiredClient = std::unique_ptr<BaseLib::HttpClient>(new BaseLib::HttpClient(_bl, _hostname, _port3, false, false));
            if(_port4 != 0) _hmVirtualClient = std::unique_ptr<BaseLib::HttpClient>(new BaseLib::HttpClient(_bl, _hostname, _port4, false, false));

            _ipAddress = BaseLib::Net::resolveHostname(_hostname);

            _bidcosIdString = "Homegear_BidCoS_" + _listenIp + "_" + std::to_string(_listenPort);
            _hmipIdString = "Homegear_HMIP_" + _listenIp + "_" + std::to_string(_listenPort);
            _wiredIdString = "Homegear_Wired_" + _listenIp + "_" + std::to_string(_listenPort);
            _hmVirtualIdString = "Homegear_Virtual_" + _listenIp + "_" + std::to_string(_listenPort);

            _stopPingThread = false;
            _bl->threadManager.start(_pingThread, true, &Ccu::ping, this);
            _bl->threadManager.start(_initThread, true, &Ccu::init, this);
        }
        IPhysicalInterface::startListening();
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void Ccu::stopListening()
{
    try
    {
        _stopPingThread = true;

        deinit();

        _stopped = true;

        _bl->threadManager.join(_pingThread);

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
            _out.printError("Error processing packet (1): " + std::string(ex.what()));
        }
        return;
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    http->reset();
}

void Ccu::processPacket(int32_t clientId, std::string& methodName, BaseLib::PArray parameters)
{
    try
    {
        BaseLib::PVariable response = std::make_shared<BaseLib::Variable>();

        if(!parameters->empty() && parameters->at(0)->stringValue == _hmipIdString) _lastPongHmip.store(BaseLib::HelperFunctions::getTime());
        else if(!parameters->empty() && parameters->at(0)->stringValue == _hmVirtualIdString) _lastPongHmVirtual.store(BaseLib::HelperFunctions::getTime());

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
                            else if(parameters->at(0)->stringValue == _hmVirtualIdString) parameters->at(0)->integerValue = (int32_t) RpcType::hmvirtual;
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
            else if(parameters->at(0)->stringValue == _hmVirtualIdString)
            {
                parameters->at(0)->integerValue = (int32_t)RpcType::hmvirtual;
                _hmVirtualNewDevicesCalled = true;
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
            else if(parameters->at(0)->stringValue == _hmVirtualIdString) parameters->at(0)->integerValue = (int32_t)RpcType::hmvirtual;
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
                else if(parameters->at(0)->stringValue == _hmVirtualIdString) parameters->at(0)->integerValue = (int32_t)RpcType::hmvirtual;
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
            }

            if(!isOpen())
            {
                auto data = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
                data->structValue->emplace("IP_ADDRESS", std::make_shared<BaseLib::Variable>(_ipAddress));
                data->structValue->emplace("SERIALNUMBER", std::make_shared<BaseLib::Variable>(_settings->serialNumber));
                if(!_unreachable)
                {
                    _unreachable = true;
                    _bl->globalServiceMessages.set(MY_FAMILY_ID, _settings->id, 0, _settings->id, BaseLib::HelperFunctions::getTimeSeconds(), "l10n.ccu.serviceMessage.ccuUnreachable", std::list<std::string>{ _settings->serialNumber, _ipAddress }, data, 1);
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

            if(_wiredClient && !_wiredDisabled && ((_wiredNewDevicesCalled && BaseLib::HelperFunctions::getTime() - _lastPongWired.load() > 3600000) || _wiredReInit))
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

            if(_hmVirtualClient && ((_hmVirtualNewDevicesCalled && BaseLib::HelperFunctions::getTime() - _lastPongHmVirtual.load() > 3600000) || _hmVirtualReInit))
            {
                if(regaReady())
                {
                    if(!_hmVirtualReInit) _out.printError("Error: No keep alive received (Virtual). Reinitializing...");
                    init();
                }
                else _hmVirtualReInit = true;
            }

            if(_port != 0 && !_bidcosClient) _bidcosClient = std::unique_ptr<BaseLib::HttpClient>(new BaseLib::HttpClient(_bl, _hostname, _port, false, false));
            if(_port2 != 0 && !_hmipClient) _hmipClient = std::unique_ptr<BaseLib::HttpClient>(new BaseLib::HttpClient(_bl, _hostname, _port2, false, false));
            if(_port3 != 0 && !_wiredClient) _wiredClient = std::unique_ptr<BaseLib::HttpClient>(new BaseLib::HttpClient(_bl, _hostname, _port3, false, false));
            if(_port4 != 0 && !_hmVirtualClient) _hmVirtualClient = std::unique_ptr<BaseLib::HttpClient>(new BaseLib::HttpClient(_bl, _hostname, _port4, false, false));

            if(_ipAddress.empty())
            {
                _ipAddress = BaseLib::Net::resolveHostname(_hostname);
                _noHost = _hostname.empty();
            }
        }
    }
    catch(const std::exception& ex)
    {
        _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

BaseLib::PVariable Ccu::invoke(Ccu::RpcType rpcType, std::string methodName, BaseLib::PArray parameters)
{
    try
    {
        if(_stopped) return BaseLib::Variable::createError(-32500, "CCU is stopped.");
        if(rpcType == RpcType::bidcos && !_bidcosClient) return BaseLib::Variable::createError(-32501, "HomeMatic BidCoS is disabled.");
        else if(rpcType == RpcType::hmip && !_hmipClient) return BaseLib::Variable::createError(-32501, "HomeMatic IP is disabled.");
        else if(rpcType == RpcType::wired && (!_wiredClient || _wiredDisabled)) return BaseLib::Variable::createError(-32501, "HomeMatic Wired is disabled.");
        else if(rpcType == RpcType::hmvirtual && !_hmVirtualClient) return BaseLib::Variable::createError(-32501, "HomeMatic Virtual Devices are disabled.");

        std::lock_guard<std::mutex> invokeGuard(_invokeMutex);

        std::string path = rpcType == RpcType::hmvirtual ? "/groups" : "/";
        std::string data;
        std::vector<char> xmlData;
        _xmlrpcEncoder->encodeRequest(methodName, parameters, xmlData);
        xmlData.push_back('\r');
        xmlData.push_back('\n');
        std::string header = "POST " + path + " HTTP/1.1\r\nUser-Agent: homegear-ccu\r\nHost: " + _ipAddress + ":" + std::to_string(_port2) + "\r\nContent-Type: text/xml\r\nContent-Length: " + std::to_string(xmlData.size()) + "\r\nConnection: Keep-Alive\r\n\r\n";
        data.reserve(header.size() + xmlData.size());
        data.insert(data.end(), header.begin(), header.end());
        data.insert(data.end(), xmlData.begin(), xmlData.end());

        BaseLib::Http httpResponse;
        try
        {
            if(GD::bl->debugLevel >= 5) GD::out.printDebug("Debug: Sending (" + std::to_string((int)rpcType) + ") " + std::string(data.begin(), data.end()));

            if(rpcType == RpcType::bidcos) _bidcosClient->sendRequest(data, httpResponse, false);
            else if(rpcType == RpcType::hmip) _hmipClient->sendRequest(data, httpResponse, false);
            else if(rpcType == RpcType::wired) _wiredClient->sendRequest(data, httpResponse, false);
            else if(rpcType == RpcType::hmvirtual) _hmVirtualClient->sendRequest(data, httpResponse, false);

            if(GD::bl->debugLevel >= 5) GD::out.printDebug("Debug: Response was (" + std::to_string((int)rpcType) + ") " + std::string(httpResponse.getContent().data(), httpResponse.getContentSize()));
        }
        catch(const std::exception& ex)
        {
            if(rpcType == RpcType::wired && methodName == "init") return BaseLib::Variable::createError(400, "Bad Request");
            else return BaseLib::Variable::createError(-1, ex.what());
        }

        if(httpResponse.getHeader().responseCode == 400 || httpResponse.getHeader().responseCode == 503) return BaseLib::Variable::createError(400, "Bad Request");
        else return _xmlrpcDecoder->decodeResponse(httpResponse.getContent());
    }
    catch(const std::exception& ex)
    {
        return BaseLib::Variable::createError(-32500, ex.what());
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
    return false;
}

std::unordered_map<std::string, std::unordered_map<int32_t, std::string>> Ccu::getNames()
{
    std::unordered_map<std::string, std::unordered_map<int32_t, std::string>> deviceNames;
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
            deviceNames[addressIterator->second->stringValue][-1] = nameIterator->second->stringValue;

            auto channelsIterator = nameElement->structValue->find("Channels");
            if(channelsIterator == nameElement->structValue->end()) continue;

            for(auto& channelElement : *channelsIterator->second->arrayValue)
            {
                auto channelIterator = channelElement->structValue->find("Address");
                auto channelNameIterator = channelElement->structValue->find("ChannelName");
                if(channelIterator == channelElement->structValue->end() || channelNameIterator == channelElement->structValue->end()) continue;

                auto addressPair = BaseLib::HelperFunctions::splitLast(channelIterator->second->stringValue, ':');
                if(addressPair.second.empty()) continue;
                int32_t channel = BaseLib::Math::getNumber(addressPair.second);

                deviceNames[addressIterator->second->stringValue][channel] = ansi.toUtf8(channelNameIterator->second->stringValue);
            }
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
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
}

}
