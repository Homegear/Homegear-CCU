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

#ifndef HOMEGEAR_CCU2_CCU2_H
#define HOMEGEAR_CCU2_CCU2_H

#include <homegear-base/Systems/IPhysicalInterface.h>
#include <homegear-base/Sockets/TcpSocket.h>
#include <homegear-base/Encoding/RpcEncoder.h>
#include <homegear-base/Encoding/RpcDecoder.h>
#include <homegear-base/Encoding/BinaryRpc.h>
#include <homegear-base/Encoding/XmlrpcDecoder.h>
#include <homegear-base/Encoding/XmlrpcEncoder.h>
#include <homegear-base/Encoding/Http.h>

namespace MyFamily
{

class Ccu2 : public BaseLib::Systems::IPhysicalInterface
{
public:
    enum class RpcType : int32_t
    {
        bidcos,
        hmip,
        wired
    };

    Ccu2(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings);
    virtual ~Ccu2();

    void startListening();
    void stopListening();
    void sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet) {};
    BaseLib::PVariable invoke(RpcType rpcType, std::string methodName, BaseLib::PArray parameters);

    virtual bool isOpen() { return _bidcosClient && _bidcosClient->connected() && (_port2 == 0 || (_hmipClient && _hmipClient->connected())); }
private:
    BaseLib::Output _out;
    bool _noHost = true;
    std::atomic_bool _stopped;
    int32_t _port = 2001;
    int32_t _port2 = 2010;
    std::string _listenIp;
    int32_t _listenPort = -1;
    std::string _bidcosIdString;
    std::string _hmipIdString;
    std::string _wiredIdString;
    std::atomic_long _lastPongBidcos;
    std::atomic_long _lastPongHmip;
    std::atomic_long _lastPongWired;
    std::shared_ptr<BaseLib::TcpSocket> _server;
    std::unique_ptr<BaseLib::TcpSocket> _bidcosClient;
    std::unique_ptr<BaseLib::TcpSocket> _hmipClient;
    std::unique_ptr<BaseLib::TcpSocket> _wiredClient;
    std::unique_ptr<BaseLib::Rpc::RpcEncoder> _rpcEncoder;
    std::unique_ptr<BaseLib::Rpc::RpcDecoder> _rpcDecoder;
    std::atomic_bool _hmipNewDevicesCalled;
    std::atomic_bool _isBinaryRpc;
    std::unique_ptr<BaseLib::Rpc::BinaryRpc> _binaryRpc;
    std::unique_ptr<BaseLib::Http> _http;
    std::unique_ptr<BaseLib::Rpc::XmlrpcEncoder> _xmlrpcEncoder;
    std::unique_ptr<BaseLib::Rpc::XmlrpcDecoder> _xmlrpcDecoder;

    std::thread _listenThread2;
    std::thread _initThread;
    std::thread _pingThread;

    std::mutex _invokeMutex;
    std::mutex _requestMutex;
    std::mutex _requestWaitMutex;
    std::condition_variable _requestConditionVariable;

    std::mutex _responseMutex;
    BaseLib::PVariable _response;

    void newConnection(int32_t clientId, std::string address, uint16_t port);
    void packetReceived(int32_t clientId, BaseLib::TcpSocket::TcpPacket packet);
    void processPacket(int32_t clientId, bool binaryRpc, std::string& methodName, BaseLib::PArray parameters);
    void listen(RpcType rpcType);
    void init();
    void ping();
};

}

#endif
