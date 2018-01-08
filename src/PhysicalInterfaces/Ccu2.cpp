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

namespace MyFamily
{

Ccu2::Ccu2(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings) : IPhysicalInterface(GD::bl, GD::family->getFamily(), settings)
{
    if(settings->listenThreadPriority == -1)
    {
        settings->listenThreadPriority = 0;
        settings->listenThreadPolicy = SCHED_OTHER;
    }

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
    if(_port < 1 || _port > 65535) _port = 80;
}

Ccu2::~Ccu2()
{
    _stopCallbackThread = true;
    _bl->threadManager.join(_listenThread);
}

void Ccu2::sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet)
{
    try
    {
        if(_noHost) return;
        if(!packet)
        {
            _out.printWarning("Warning: Packet was nullptr.");
            return;
        }



        _lastPacketSent = BaseLib::HelperFunctions::getTime();
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
            BaseLib::TcpSocket::TcpServerInfo serverInfo;
            serverInfo.newConnectionCallback = std::bind(&Ccu2::newConnection, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
            serverInfo.packetReceivedCallback = std::bind(&Ccu2::packetReceived, this, std::placeholders::_1, std::placeholders::_2);

            _server = std::make_shared<BaseLib::TcpSocket>(GD::bl, serverInfo);
            std::string listenAddress;
            _server->startServer("0.0.0.0", listenAddress, _listenPort);
            _out.printInfo("RPC server started listening on " + listenAddress + ":" + std::to_string(_listenPort));

            _client = std::unique_ptr<BaseLib::TcpSocket>(new BaseLib::TcpSocket(_bl, _hostname, std::to_string(_port)));
            _ipAddress = _client->getIpAddress();
            _noHost = _hostname.empty();

            if(_settings->listenThreadPriority > -1) _bl->threadManager.start(_listenThread, true, _settings->listenThreadPriority, _settings->listenThreadPolicy, &Ccu2::listen, this);
            else _bl->threadManager.start(_listenThread, true, &Ccu2::listen, this);
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
        _stopCallbackThread = true;
        _bl->threadManager.join(_listenThread);
        _stopCallbackThread = false;

        if(_client) _client->close();
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
        _out.printInfo("Info: New connection from " + address + " on port " + std::to_string(port));
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

void Ccu2::listen()
{

}

}