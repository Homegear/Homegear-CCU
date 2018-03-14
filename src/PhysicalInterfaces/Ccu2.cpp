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

#include "Ccu2.h"
#include "../GD.h"
#include "../MyPacket.h"

namespace MyFamily
{

Ccu2::Ccu2(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings) : IPhysicalInterface(GD::bl, GD::family->getFamily(), settings)
{
    if(settings->listenThreadPriority == -1)
    {
        settings->listenThreadPriority = 0;
        settings->listenThreadPolicy = SCHED_OTHER;
    }

    _rpcDecoder.reset(new BaseLib::Rpc::RpcDecoder(GD::bl, true, true));
    _rpcEncoder.reset(new BaseLib::Rpc::RpcEncoder(GD::bl, false, false));
    _xmlrpcDecoder.reset(new BaseLib::Rpc::XmlrpcDecoder(GD::bl));
    _xmlrpcEncoder.reset(new BaseLib::Rpc::XmlrpcEncoder(GD::bl));

    _bidcosDevicesExist = false;
    _hmipNewDevicesCalled = false;
    _wiredNewDevicesCalled = false;
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
}

Ccu2::~Ccu2()
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

void Ccu2::reconnect(RpcType rpcType, bool forceReinit)
{
    try
    {
        if(rpcType == RpcType::bidcos)
        {
            _bidcosClient->close();
            _bidcosClient->open();
            _bidcosReInit = true;
        }
        else if(rpcType == RpcType::wired)
        {
            _wiredClient->close();
            _wiredClient->open();
            _wiredReInit = true;
        }
        else if(rpcType == RpcType::hmip)
        {
            _hmipClient->close();
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

void Ccu2::init()
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

        if(_bidcosClient)
        {
            try
            {
                auto parameters = std::make_shared<BaseLib::Array>();
                parameters->reserve(2);
                parameters->push_back(std::make_shared<BaseLib::Variable>("binary://" + _listenIp + ":" + std::to_string(_listenPort)));
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

        _out.printInfo("Info: Init complete.");
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

void Ccu2::deinit()
{
    try
    {
        BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
        parameters->reserve(2);
        parameters->push_back(std::make_shared<BaseLib::Variable>("binary://" + _listenIp + ":" + std::to_string(_listenPort)));
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

void Ccu2::startListening()
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
            serverInfo.newConnectionCallback = std::bind(&Ccu2::newConnection, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
            serverInfo.connectionClosedCallback = std::bind(&Ccu2::connectionClosed, this, std::placeholders::_1);
            serverInfo.packetReceivedCallback = std::bind(&Ccu2::packetReceived, this, std::placeholders::_1, std::placeholders::_2);

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

            _out.printInfo("Info: Connecting to IP " + _hostname + " and ports " + std::to_string(_port) + ", " + std::to_string(_port3) + (_port2 != 0 ? ", " + std::to_string(_port2) : "") + ".");

            if(_port != 0)
            {
                try
                {
                    _bidcosClient = std::unique_ptr<BaseLib::TcpSocket>(new BaseLib::TcpSocket(_bl, _hostname, std::to_string(_port)));
                    _bidcosClient->open();
                }
                catch(BaseLib::Exception& ex)
                {
                    _out.printError("Could not connect to HomeMatic BidCoS port.");
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
                if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &Ccu2::listen, this, RpcType::bidcos);
                else _bl->threadManager.start(_listenThread, true, &Ccu2::listen, this, RpcType::bidcos);
            }

            if(_hmipClient && _port2 != 0)
            {
                if(!_bidcosClient) _connectedRpcType = RpcType::hmip;

                if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread2, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &Ccu2::listen, this, RpcType::hmip);
                else _bl->threadManager.start(_listenThread2, true, &Ccu2::listen, this, RpcType::hmip);
            }

            if(_wiredClient && _port3 != 0)
            {
                if(!_bidcosClient && _connectedRpcType == RpcType::bidcos) _connectedRpcType = RpcType::wired;

                if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread3, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &Ccu2::listen, this, RpcType::wired);
                else _bl->threadManager.start(_listenThread3, true, &Ccu2::listen, this, RpcType::wired);
            }
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

void Ccu2::stopListening()
{
    try
    {
        _stopPingThread = true;
        _bl->threadManager.join(_pingThread);

        deinit();

        _stopped = true;
        _stopCallbackThread = true;
        _bl->threadManager.join(_listenThread);
        _stopCallbackThread = false;

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

void Ccu2::newConnection(int32_t clientId, std::string address, uint16_t port)
{
    try
    {
        if(GD::bl->debugLevel >= 5) _out.printDebug("Debug: New connection from " + address + " on port " + std::to_string(port) + ". Client ID is: " + std::to_string(clientId));
        CcuClientInfo clientInfo;
        clientInfo.binaryRpc = std::make_shared<BaseLib::Rpc::BinaryRpc>(_bl);
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

void Ccu2::connectionClosed(int32_t clientId)
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

void Ccu2::packetReceived(int32_t clientId, BaseLib::TcpSocket::TcpPacket packet)
{
    try
    {
        if(GD::bl->debugLevel >= 5) _out.printDebug("Debug: Raw packet " + BaseLib::HelperFunctions::getHexString(packet));

        std::shared_ptr<BaseLib::Rpc::BinaryRpc> binaryRpc;
        std::shared_ptr<BaseLib::Http> http;

        {
            std::lock_guard<std::mutex> ccuClientInfoGuard(_ccuClientInfoMutex);
            auto clientIterator = _ccuClientInfo.find(clientId);
            if(clientIterator == _ccuClientInfo.end())
            {
                _out.printError("Error: Client with ID " + std::to_string(clientId) + " not found in map.");
                return;
            }
            binaryRpc = clientIterator->second.binaryRpc;
            http = clientIterator->second.http;
        }

        if(packet.empty()) return;
        bool isBinaryRpc = false;
        uint32_t processedBytes = 0;
        try
        {
            while(processedBytes < packet.size())
            {
                if(!isBinaryRpc && !binaryRpc->processingStarted() && !http->headerProcessingStarted())
                {
                    isBinaryRpc = packet.size() - processedBytes < 3 ? (char)(*(packet.data() + processedBytes)) == 'B' : !strncmp((char*)(packet.data() + processedBytes), "Bin", 3);
                }

                std::string methodName;
                BaseLib::PArray parameters;

                if(isBinaryRpc)
                {
                    processedBytes += binaryRpc->process((char*)packet.data() + processedBytes, packet.size() - processedBytes);
                    if(binaryRpc->isFinished())
                    {
                        if(binaryRpc->getType() == BaseLib::Rpc::BinaryRpc::Type::request)
                        {
                            parameters = _rpcDecoder->decodeRequest(binaryRpc->getData(), methodName);
                            processPacket(clientId, true, methodName, parameters);
                        }
                        isBinaryRpc = false;
                        binaryRpc->reset();
                    }
                }
                else
                {
                    processedBytes += http->process((char*)packet.data() + processedBytes, packet.size() - processedBytes);
                    if(http->isFinished())
                    {
                        if(http->getHeader().method == "POST")
                        {
                            parameters = _xmlrpcDecoder->decodeRequest(http->getContent(), methodName);
                            processPacket(clientId, false, methodName, parameters);
                        }
                        http->reset();
                    }
                }
            }
        }
        catch(BaseLib::Rpc::BinaryRpcException& ex)
        {
            _out.printError("Error processing packet (1): " + ex.what());
            binaryRpc->reset();
            http->reset();
        }
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
}

void Ccu2::processPacket(int32_t clientId, bool binaryRpc, std::string& methodName, BaseLib::PArray parameters)
{
    try
    {
        BaseLib::PVariable response = std::make_shared<BaseLib::Variable>();

        if(!binaryRpc) _lastPongHmip.store(BaseLib::HelperFunctions::getTime());

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
                        if(_bl->debugLevel >= 5) _out.printDebug("Debug: Pong received");
                        if(parameters->at(0)->stringValue == _bidcosIdString) _lastPongBidcos.store(BaseLib::HelperFunctions::getTime());
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
            if(!binaryRpc) _hmipNewDevicesCalled = true;
            if(!binaryRpc) parameters->at(0)->integerValue = (int32_t)RpcType::hmip;
            else if(parameters->at(0)->stringValue == _bidcosIdString)
            {
                parameters->at(0)->integerValue = (int32_t) RpcType::bidcos;
                _bidcosDevicesExist = parameters->at(1)->arrayValue->size() > 52;
            }
            else if(parameters->at(0)->stringValue == _wiredIdString)
            {
                parameters->at(0)->integerValue = (int32_t)RpcType::wired;
                _wiredNewDevicesCalled = true;
            }
            _out.printInfo("Info: CCU (" + std::to_string(parameters->at(0)->integerValue) + ") is calling RPC method " + methodName);
            PMyPacket packet = std::make_shared<MyPacket>(methodName, parameters);
            raisePacketReceived(packet);
        }
        else if(methodName == "system.listMethods" || methodName == "listDevices")
        {
            if(!binaryRpc) parameters->at(0)->integerValue = (int32_t)RpcType::hmip;
            else if(parameters->at(0)->stringValue == _bidcosIdString) parameters->at(0)->integerValue = (int32_t) RpcType::bidcos;
            else if(parameters->at(0)->stringValue == _wiredIdString) parameters->at(0)->integerValue = (int32_t)RpcType::wired;
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
        if(binaryRpc)
        {
            _rpcEncoder->encodeResponse(response, responsePacket);
            _server->sendToClient(clientId, responsePacket, true);
        }
        else
        {
            std::vector<uint8_t> xmlData;
            _xmlrpcEncoder->encodeResponse(response, xmlData);
            std::string header = "HTTP/1.1 200 OK\r\nConnection: Close\r\nContent-Type: text/xml\r\nContent-Length: " + std::to_string(xmlData.size()) + "\r\n\r\n";
            responsePacket.reserve(header.size() + xmlData.size());
            responsePacket.insert(responsePacket.end(), header.begin(), header.end());
            responsePacket.insert(responsePacket.end(), xmlData.begin(), xmlData.end());
            _server->sendToClient(clientId, responsePacket, true);
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

void Ccu2::listen(Ccu2::RpcType rpcType)
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
            _bl->threadManager.start(_initThread, true, &Ccu2::init, this);
            _stopPingThread = false;
            _bl->threadManager.start(_pingThread, true, &Ccu2::ping, this);
        }

        while(!_stopped && !_stopCallbackThread)
        {
            try
            {
                try
                {
                    if(rpcType == RpcType::bidcos) bytesRead = _bidcosClient->proofread(buffer.data(), buffer.size());
                    if(rpcType == RpcType::wired) bytesRead = _wiredClient->proofread(buffer.data(), buffer.size());
                    else if(rpcType == RpcType::hmip)
                    {
                        bytesRead = _hmipClient->proofread(buffer.data(), buffer.size());
                    }
                }
                catch(SocketTimeOutException& ex)
                {
                    if(ex.getType() == SocketTimeOutException::SocketTimeOutType::readTimeout) reconnect(rpcType, true);
                    continue;
                }

                if(bytesRead > buffer.size()) bytesRead = buffer.size();

                try
                {
                    processedBytes = 0;
                    while(processedBytes < bytesRead)
                    {
                        if(rpcType == RpcType::bidcos || rpcType == RpcType::wired)
                        {
                            processedBytes += binaryRpc.process(buffer.data() + processedBytes, bytesRead - processedBytes);
                            if(binaryRpc.isFinished())
                            {
                                if(binaryRpc.getType() == BaseLib::Rpc::BinaryRpc::Type::response)
                                {
                                    std::unique_lock<std::mutex> waitLock(_requestWaitMutex);
                                    {
                                        std::lock_guard<std::mutex> responseGuard(_responseMutex);
                                        _response = _rpcDecoder->decodeResponse(binaryRpc.getData());
                                    }
                                    waitLock.unlock();
                                    _requestConditionVariable.notify_all();
                                }
                                binaryRpc.reset();
                            }
                        }
                        else if(rpcType == RpcType::hmip)
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
            }
            catch(BaseLib::Exception& ex)
            {
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            catch(...)
            {
                _out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
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

void Ccu2::ping()
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

            if(_bidcosDevicesExist)
            {
                BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();
                parameters->push_back(std::make_shared<BaseLib::Variable>(_bidcosIdString));
                auto result = invoke(RpcType::bidcos, "ping", parameters);
                if(result->errorStruct)
                {
                    _out.printError("Error calling \"ping\" (BidCoS): " + result->structValue->at("faultString")->stringValue);
                    continue;
                }
            }

            if((_bidcosDevicesExist && BaseLib::HelperFunctions::getTime() - _lastPongBidcos.load() > 70000) || _bidcosReInit)
            {
                if(regaReady())
                {
                    _out.printError("Error: No keep alive response received (BidCoS). Reinitializing...");
                    init();
                }
                else _bidcosReInit = true;
            }

            if((_wiredNewDevicesCalled && BaseLib::HelperFunctions::getTime() - _lastPongWired.load() > 3600000) || _wiredReInit)
            {
                if(regaReady())
                {
                    _out.printError("Error: No keep alive received (Wired). Reinitializing...");
                    init();
                }
                else _wiredReInit = true;
            }

            if((_hmipNewDevicesCalled && BaseLib::HelperFunctions::getTime() - _lastPongHmip.load() > 3600000) || _hmipReInit)
            {
                if(regaReady())
                {
                    _out.printError("Error: No keep alive received (HM-IP). Reinitializing...");
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
                    if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread2, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &Ccu2::listen, this, RpcType::bidcos);
                    else _bl->threadManager.start(_listenThread2, true, &Ccu2::listen, this, RpcType::bidcos);
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
                    if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread2, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &Ccu2::listen, this, RpcType::hmip);
                    else _bl->threadManager.start(_listenThread2, true, &Ccu2::listen, this, RpcType::hmip);
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
                    if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread3, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &Ccu2::listen, this, RpcType::wired);
                    else _bl->threadManager.start(_listenThread3, true, &Ccu2::listen, this, RpcType::wired);
                }
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

BaseLib::PVariable Ccu2::invoke(Ccu2::RpcType rpcType, std::string methodName, BaseLib::PArray parameters, bool wait)
{
    try
    {
        if(_stopped) return BaseLib::Variable::createError(-32500, "CCU2 is stopped.");
        if(rpcType == RpcType::hmip && !_hmipClient) return BaseLib::Variable::createError(-32501, "HomeMatic IP is disabled.");
        else if(rpcType == RpcType::wired && !_wiredClient) return BaseLib::Variable::createError(-32501, "HomeMatic Wired is disabled.");

        std::lock_guard<std::mutex> invokeGuard(_invokeMutex);

        std::vector<char> data;
        if(rpcType == RpcType::bidcos || rpcType == RpcType::wired) _rpcEncoder->encodeRequest(methodName, parameters, data);
        else if(rpcType == RpcType::hmip)
        {
            std::vector<char> xmlData;
            _xmlrpcEncoder->encodeRequest(methodName, parameters, xmlData);
            xmlData.push_back('\r');
            xmlData.push_back('\n');
            std::string header = "POST / HTTP/1.1\r\nUser-Agent: homegear-ccu\r\nHost: " + _ipAddress + ":" + std::to_string(_port2) + "\r\nContent-Type: text/xml\r\nContent-Length: " + std::to_string(xmlData.size()) + "\r\nConnection: Keep-Alive\r\n\r\n";
            data.reserve(header.size() + xmlData.size());
            data.insert(data.end(), header.begin(), header.end());
            data.insert(data.end(), xmlData.begin(), xmlData.end());
        }

        if(wait)
        {
            std::lock_guard<std::mutex> responseGuard(_responseMutex);
            _response = std::make_shared<BaseLib::Variable>();
        }

        if(rpcType == RpcType::bidcos) _bidcosClient->proofwrite(data);
        else if(rpcType == RpcType::hmip) _hmipClient->proofwrite(data);
        else if(rpcType == RpcType::wired) _wiredClient->proofwrite(data);

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
    return BaseLib::Variable::createError(-32500, "Unknown application error.");
}

bool Ccu2::regaReady()
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

}