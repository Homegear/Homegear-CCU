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

#ifndef HOMEGEAR_CCU_CCU_H
#define HOMEGEAR_CCU_CCU_H

#include <homegear-base/Systems/IPhysicalInterface.h>
#include <homegear-base/Encoding/XmlrpcDecoder.h>
#include <homegear-base/Encoding/XmlrpcEncoder.h>
#include <homegear-base/Encoding/Http.h>
#include <homegear-base/Sockets/HttpClient.h>
#include <c1-net/TcpServer.h>

namespace MyFamily
{

class Ccu : public BaseLib::Systems::IPhysicalInterface
{
public:
    enum class RpcType : int32_t
    {
        bidcos,
        hmip,
        wired,
        hmvirtual
    };

    struct CcuServiceMessage
    {
        std::string serial;
        std::string message;
        bool value = false;
        int32_t time = 0;
    };

    Ccu(std::shared_ptr<BaseLib::Systems::PhysicalInterfaceSettings> settings);
    virtual ~Ccu();

    std::string getSerialNumber() { return _settings->serialNumber; }
    std::string getPort1() { return _settings->port; }
    std::string getPort2() { return _settings->port2; }
    std::string getPort3() { return _settings->port3; }
    std::string getPort4() { return _settings->port4; }

    bool hasBidCos() { return (bool)_bidcosClient; }
    bool hasWired() { return (bool)_wiredClient && !_wiredDisabled.load(std::memory_order_acquire); }
    bool hasHmip() { return (bool)_hmipClient; }
    bool hasHmVirtual() { return (bool)_hmVirtualClient; }

    std::vector<std::shared_ptr<CcuServiceMessage>> getServiceMessages() { std::lock_guard<std::mutex> serviceMessagesGuard(_serviceMessagesMutex); return _serviceMessages; }
    std::unordered_map<std::string, std::unordered_map<int32_t, std::string>> getNames();

    void startListening();
    void stopListening();
    void sendPacket(std::shared_ptr<BaseLib::Systems::Packet> packet) {};
    BaseLib::PVariable invoke(RpcType rpcType, std::string methodName, BaseLib::PArray parameters);

    virtual bool isOpen() { return _bidcosClient || _hmipClient || _wiredClient; }
private:
    struct CcuClientInfo
    {
        std::shared_ptr<BaseLib::Http> http;
    };

    BaseLib::Output _out;
    bool _noHost = true;
    std::atomic_bool _stopped{true};
    int32_t _port = 2001;
    int32_t _port2 = 2010;
    int32_t _port3 = 2000;
    int32_t _port4 = 9292;
    std::string _listenIp;
    int32_t _listenPort = -1;
    std::string _bidcosIdString;
    std::string _hmipIdString;
    std::string _wiredIdString;
    std::string _hmVirtualIdString;
    std::atomic_bool _stopPingThread{false};
    std::atomic<int64_t> _lastPongBidcos{0};
    std::atomic<int64_t> _lastPongHmip{0};
    std::atomic<int64_t> _lastPongWired{0};
    std::atomic<int64_t> _lastPongHmVirtual{0};
    std::shared_ptr<C1Net::TcpServer> _server;
    std::unique_ptr<BaseLib::HttpClient> _bidcosClient;
    std::unique_ptr<BaseLib::HttpClient> _hmipClient;
    std::unique_ptr<BaseLib::HttpClient> _wiredClient;
    std::unique_ptr<BaseLib::HttpClient> _hmVirtualClient;
    std::unique_ptr<BaseLib::HttpClient> _httpClient;
    RpcType _connectedRpcType = RpcType::bidcos;
    std::atomic_bool _unreachable{false};
    std::atomic_bool _bidcosDevicesExist{false};
    std::atomic_bool _bidcosReInit{false};
    std::atomic_bool _hmipNewDevicesCalled{false};
    std::atomic_bool _hmipReInit{false};
    std::atomic_bool _wiredNewDevicesCalled{false};
    std::atomic_bool _wiredReInit{false};
    std::atomic_bool _wiredDisabled{false};
    std::atomic_bool _hmVirtualNewDevicesCalled{false};
    std::atomic_bool _hmVirtualReInit{false};
    std::mutex _ccuClientInfoMutex;
    std::map<int32_t, CcuClientInfo> _ccuClientInfo;
    std::unique_ptr<BaseLib::Rpc::XmlrpcEncoder> _xmlrpcEncoder;
    std::unique_ptr<BaseLib::Rpc::XmlrpcDecoder> _xmlrpcDecoder;

    std::thread _initThread;
    std::thread _pingThread;

    std::mutex _reconnectMutex;

    std::mutex _invokeMutex;

    std::string _getServiceMessagesScript = "Write('{ \"serviceMessages\":[');\nboolean isFirst = true;\nstring serviceID;\nforeach (serviceID, dom.GetObject(ID_SERVICES).EnumUsedIDs())\n{\n  object serviceObj = dom.GetObject(serviceID);\n  integer state = serviceObj.AlState();\n  if (state == 1)\n  {\n    string err = serviceObj.Name().StrValueByIndex (\".\", 1);\n    object alObj = serviceObj.AlTriggerDP();\n    object chObj = dom.GetObject(dom.GetObject(alObj).Channel());\n    object devObj = dom.GetObject(chObj.Device());\n    string strDate = serviceObj.Timestamp().Format(\"%s\");\n    if (isFirst) { isFirst = false; } else { WriteLine(\",\"); }\n    Write('{\"address\":\"' # devObj.Address() # '\", \"state\":\"' # state # '\", \"message\":\"' # err # '\", \"time\":\"' # strDate # '\"}');\n  }\n}\nWrite(\"]}\");";
    std::string _getNamesScript = "string sDevId;\nstring sChnId;\nstring sDPId;\nstring channelId;\nWrite('{');\n    boolean dFirst = true;\n    Write('\"Devices\":[');\n    foreach (sDevId, root.Devices().EnumUsedIDs()) {\n        object oDevice   = dom.GetObject(sDevId);\n        boolean bDevReady = oDevice.ReadyConfig();\n        string sDevInterfaceId = oDevice.Interface();\n        string sDevInterface   = dom.GetObject(sDevInterfaceId).Name();\n        if (bDevReady) {\n            if (dFirst) {\n                dFirst = false;\n            } else {\n                WriteLine(',');\n            }\n            Write('{');\n            Write('\"ID\":\"' # oDevice.ID());\n            Write('\",\"Name\":\"' # oDevice.Name());\n            Write('\",\"TypeName\":\"' # oDevice.TypeName());\n            Write('\",\"HssType\":\"' # oDevice.HssType() # '\",\"Address\":\"' # oDevice.Address() # '\",\"Interface\":\"' # sDevInterface # '\"');\n            Write(',\"Channels\":[');\n            boolean bFirstSecond = true;\n            foreach(channelId, oDevice.Channels()) {      \n                if (bFirstSecond == false) {\n                    Write(',');\n                } else {\n                    bFirstSecond = false;\n                }\n                var channel = dom.GetObject(channelId);\n                Write('{');\n                Write('\"ChannelName\":' # '\"' # channel.Name() # '\"');\n                Write(',\"Address\":' # '\"' # channel.Address() # '\"');\n                Write('}');\n            }\n            Write(']');\n            Write('}');\n        }\n    }\nWrite(']}');";

    std::mutex _serviceMessagesMutex;
    std::vector<std::shared_ptr<CcuServiceMessage>> _serviceMessages;

    void log(uint32_t log_level, const std::string &message);
    void newConnection(const C1Net::TcpServer::PTcpClientData &client_data);
    void connectionClosed(const C1Net::TcpServer::PTcpClientData &client_data, int32_t error_code, std::string error_message);
    void packetReceived(const C1Net::TcpServer::PTcpClientData &client_data, const C1Net::TcpPacket &packet);
    void processPacket(const C1Net::TcpServer::PTcpClientData &client_data, std::string& methodName, BaseLib::PArray parameters);
    void init();
    void deinit();
    void ping();
    bool regaReady();
    void getCcuServiceMessages();
};

}

#endif
