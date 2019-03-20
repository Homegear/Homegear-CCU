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

#include "MyPeer.h"

#include "GD.h"
#include "MyPacket.h"
#include "MyCentral.h"

namespace MyFamily
{
std::shared_ptr<BaseLib::Systems::ICentral> MyPeer::getCentral()
{
    try
    {
        if(_central) return _central;
        _central = GD::family->getCentral();
        return _central;
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
    return std::shared_ptr<BaseLib::Systems::ICentral>();
}

MyPeer::MyPeer(uint32_t parentID, IPeerEventSink* eventHandler) : BaseLib::Systems::Peer(GD::bl, parentID, eventHandler)
{
    init();
}

MyPeer::MyPeer(int32_t id, int32_t address, std::string serialNumber, uint32_t parentID, IPeerEventSink* eventHandler) : BaseLib::Systems::Peer(GD::bl, id, address, serialNumber, parentID, eventHandler)
{
    init();
}

MyPeer::~MyPeer()
{
    try
    {
        dispose();
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

void MyPeer::init()
{
    try
    {

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

void MyPeer::dispose()
{
    if(_disposing) return;
    Peer::dispose();
}

void MyPeer::homegearStarted()
{
    try
    {
        Peer::homegearStarted();
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

void MyPeer::homegearShuttingDown()
{
    try
    {
        _shuttingDown = true;
        Peer::homegearShuttingDown();
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

void MyPeer::worker()
{
    try
    {
        for(auto& channel : _rpcDevice->functions)
        {
            getParamset(BaseLib::PRpcClientInfo(), channel.first, BaseLib::DeviceDescription::ParameterGroup::Type::Enum::config, 0, -1, false);
        }

        for(auto& channel : _rpcDevice->functions)
        {
            getParamset(BaseLib::PRpcClientInfo(), channel.first, BaseLib::DeviceDescription::ParameterGroup::Type::Enum::variables, 0, -1, false);
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

std::string MyPeer::handleCliCommand(std::string command)
{
    try
    {
        std::ostringstream stringStream;

        if(command == "help")
        {
            stringStream << "List of commands:" << std::endl << std::endl;
            stringStream << "For more information about the individual command type: COMMAND help" << std::endl << std::endl;
            stringStream << "unselect      Unselect this peer" << std::endl;
            stringStream << "channel count Print the number of channels of this peer" << std::endl;
            stringStream << "config print  Prints all configuration parameters and their values" << std::endl;
            return stringStream.str();
        }
        if(command.compare(0, 13, "channel count") == 0)
        {
            std::stringstream stream(command);
            std::string element;
            int32_t index = 0;
            while(std::getline(stream, element, ' '))
            {
                if(index < 2)
                {
                    index++;
                    continue;
                }
                else if(index == 2)
                {
                    if(element == "help")
                    {
                        stringStream << "Description: This command prints this peer's number of channels." << std::endl;
                        stringStream << "Usage: channel count" << std::endl << std::endl;
                        stringStream << "Parameters:" << std::endl;
                        stringStream << "  There are no parameters." << std::endl;
                        return stringStream.str();
                    }
                }
                index++;
            }

            stringStream << "Peer has " << _rpcDevice->functions.size() << " channels." << std::endl;
            return stringStream.str();
        }
        else if(command.compare(0, 12, "config print") == 0)
        {
            std::stringstream stream(command);
            std::string element;
            int32_t index = 0;
            while(std::getline(stream, element, ' '))
            {
                if(index < 2)
                {
                    index++;
                    continue;
                }
                else if(index == 2)
                {
                    if(element == "help")
                    {
                        stringStream << "Description: This command prints all configuration parameters of this peer. The values are in BidCoS packet format." << std::endl;
                        stringStream << "Usage: config print" << std::endl << std::endl;
                        stringStream << "Parameters:" << std::endl;
                        stringStream << "  There are no parameters." << std::endl;
                        return stringStream.str();
                    }
                }
                index++;
            }

            return printConfig();
        }
        else return "Unknown command.\n";
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
    return "Error executing command. See log file for more details.\n";
}

std::string MyPeer::printConfig()
{
    try
    {
        std::ostringstream stringStream;
        stringStream << "MASTER" << std::endl;
        stringStream << "{" << std::endl;
        for(std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>>::iterator i = configCentral.begin(); i != configCentral.end(); ++i)
        {
            stringStream << "\t" << "Channel: " << std::dec << i->first << std::endl;
            stringStream << "\t{" << std::endl;
            for(std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator j = i->second.begin(); j != i->second.end(); ++j)
            {
                stringStream << "\t\t[" << j->first << "]: ";
                if(!j->second.rpcParameter) stringStream << "(No RPC parameter) ";
                std::vector<uint8_t> parameterData = j->second.getBinaryData();
                for(std::vector<uint8_t>::const_iterator k = parameterData.begin(); k != parameterData.end(); ++k)
                {
                    stringStream << std::hex << std::setfill('0') << std::setw(2) << (int32_t)*k << " ";
                }
                stringStream << std::endl;
            }
            stringStream << "\t}" << std::endl;
        }
        stringStream << "}" << std::endl << std::endl;

        stringStream << "VALUES" << std::endl;
        stringStream << "{" << std::endl;
        for(std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>>::iterator i = valuesCentral.begin(); i != valuesCentral.end(); ++i)
        {
            stringStream << "\t" << "Channel: " << std::dec << i->first << std::endl;
            stringStream << "\t{" << std::endl;
            for(std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator j = i->second.begin(); j != i->second.end(); ++j)
            {
                stringStream << "\t\t[" << j->first << "]: ";
                if(!j->second.rpcParameter) stringStream << "(No RPC parameter) ";
                std::vector<uint8_t> parameterData = j->second.getBinaryData();
                for(std::vector<uint8_t>::const_iterator k = parameterData.begin(); k != parameterData.end(); ++k)
                {
                    stringStream << std::hex << std::setfill('0') << std::setw(2) << (int32_t)*k << " ";
                }
                stringStream << std::endl;
            }
            stringStream << "\t}" << std::endl;
        }
        stringStream << "}" << std::endl << std::endl;

        return stringStream.str();
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
    return "";
}

std::string MyPeer::getPhysicalInterfaceId()
{
    return _physicalInterfaceId;
}

void MyPeer::setPhysicalInterfaceId(std::string id)
{
    auto interface = GD::interfaces->getInterface(id);
    if(id.empty() || interface)
    {
        _physicalInterfaceId = id;
        setPhysicalInterface(id.empty() ? GD::interfaces->getDefaultInterface() : interface);
        saveVariable(19, _physicalInterfaceId);
    }
}

void MyPeer::setPhysicalInterface(std::shared_ptr<Ccu> interface)
{
    try
    {
        if(!interface) return;
        _physicalInterface = interface;
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

void MyPeer::loadVariables(BaseLib::Systems::ICentral* central, std::shared_ptr<BaseLib::Database::DataTable>& rows)
{
    try
    {
        if(!rows) rows = _bl->db->getPeerVariables(_peerID);
        Peer::loadVariables(central, rows);

        _rpcDevice = GD::family->getRpcDevices()->find(_deviceType, _firmwareVersion, -1);
        if(!_rpcDevice) return;

        for(BaseLib::Database::DataTable::iterator row = rows->begin(); row != rows->end(); ++row)
        {
            switch(row->second.at(2)->intValue)
            {
                case 19:
                {
                    _physicalInterfaceId = row->second.at(4)->textValue;
                    auto interface = GD::interfaces->getInterface(_physicalInterfaceId);
                    if(!_physicalInterfaceId.empty() && interface) setPhysicalInterface(interface);
                    break;
                }
                case 20:
                    _rpcType = (Ccu::RpcType)row->second.at(3)->intValue;
                    break;
            }
        }
        if(!_physicalInterface)
        {
            GD::out.printError("Error: Could not find correct physical interface for peer " + std::to_string(_peerID) + ". The peer might not work correctly.");
            _physicalInterface = GD::interfaces->getDefaultInterface();
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

void MyPeer::saveVariables()
{
    try
    {
        if(_peerID == 0) return;
        Peer::saveVariables();
        saveVariable(19, _physicalInterfaceId);
        saveVariable(20, (int32_t)_rpcType);
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

bool MyPeer::load(BaseLib::Systems::ICentral* central)
{
    try
    {
        std::shared_ptr<BaseLib::Database::DataTable> rows;
        loadVariables(central, rows);
        if(!_rpcDevice)
        {
            GD::out.printError("Error loading peer " + std::to_string(_peerID) + ": Device type not found: 0x" + BaseLib::HelperFunctions::getHexString(_deviceType) + " Firmware version: " + std::to_string(_firmwareVersion));
            return false;
        }

        initializeTypeString();
        std::string entry;
        loadConfig();
        initializeCentralConfig();

        serviceMessages.reset(new BaseLib::Systems::ServiceMessages(_bl, _peerID, _serialNumber, this));
        serviceMessages->load();

        return true;
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
    return false;
}

void MyPeer::setRssiDevice(uint8_t rssi)
{
    try
    {
        if(_disposing || rssi == 0) return;
        uint32_t time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if(time - _lastRssiDevice > 10)
        {
            _lastRssiDevice = time;

            std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>>::iterator channelIterator = valuesCentral.find(0);
            if(channelIterator == valuesCentral.end()) return;
            std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator parameterIterator = channelIterator->second.find("RSSI_DEVICE");
            if(parameterIterator == channelIterator->second.end()) return;

            BaseLib::Systems::RpcConfigurationParameter& parameter = parameterIterator->second;
            std::vector<uint8_t> parameterData{ rssi };
            parameter.setBinaryData(parameterData);

            std::shared_ptr<std::vector<std::string>> valueKeys(new std::vector<std::string>({std::string("RSSI_DEVICE")}));
            std::shared_ptr<std::vector<PVariable>> rpcValues(new std::vector<PVariable>());
            rpcValues->push_back(parameter.rpcParameter->convertFromPacket(parameterData));

            std::string eventSource = "device-" + std::to_string(_peerID);
            std::string address = _serialNumber + ":0";
            raiseEvent(eventSource, _peerID, 0, valueKeys, rpcValues);
            raiseRPCEvent(eventSource, _peerID, 0, address, valueKeys, rpcValues);
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

void MyPeer::packetReceived(PMyPacket& packet)
{
    try
    {
        if(_disposing || !packet || !_rpcDevice) return;
        if(packet->getMethodName() != "event") return;

        auto addressPair = BaseLib::HelperFunctions::splitFirst(packet->getParameters()->at(1)->stringValue, ':');
        int32_t channel = BaseLib::Math::getNumber(addressPair.second);

        std::string variableName = packet->getParameters()->at(2)->stringValue;
        BaseLib::PVariable value = packet->getParameters()->at(3);

        auto channelIterator = valuesCentral.find(channel);
        if(channelIterator == valuesCentral.end()) return;

        auto variableIterator = channelIterator->second.find(variableName);
        if(variableIterator == channelIterator->second.end()) return;

        BaseLib::Systems::RpcConfigurationParameter& parameter = variableIterator->second;
        if(!parameter.rpcParameter) return;

        std::vector<uint8_t> binaryValue;
        parameter.rpcParameter->convertToPacket(value, binaryValue);
        parameter.setBinaryData(binaryValue);
        if(parameter.databaseId > 0) saveParameter(parameter.databaseId, binaryValue);
        else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, variableName, binaryValue);
        if(_bl->debugLevel >= 4) GD::out.printInfo("Info: " + variableName + " of peer " + std::to_string(_peerID) + " with serial number " + _serialNumber + ":" + std::to_string(channel) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(binaryValue) + ".");

        std::map<uint32_t, std::shared_ptr<std::vector<std::string>>> valueKeys;
        std::map<uint32_t, std::shared_ptr<std::vector<PVariable>>> rpcValues;
        valueKeys[channel] = std::make_shared<std::vector<std::string>>();
        rpcValues[channel] = std::make_shared<std::vector<PVariable>>();
        valueKeys[channel]->push_back(variableName);
        rpcValues[channel]->push_back(parameter.rpcParameter->convertFromPacket(binaryValue, true));

        for(std::map<uint32_t, std::shared_ptr<std::vector<std::string>>>::iterator j = valueKeys.begin(); j != valueKeys.end(); ++j)
        {
            if(j->second->empty()) continue;

            std::string eventSource = "device-" + std::to_string(_peerID);
            std::string address(_serialNumber + ":" + std::to_string(j->first));
            raiseEvent(eventSource, _peerID, j->first, j->second, rpcValues.at(j->first));
            raiseRPCEvent(eventSource, _peerID, j->first, address, j->second, rpcValues.at(j->first));
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

PVariable MyPeer::getValueFromDevice(PParameter& parameter, int32_t channel, bool asynchronous)
{
    try
    {
        auto interface = GD::interfaces->getInterface(_physicalInterfaceId);
        if(!interface)
        {
            GD::out.printError("Error: Peer " + std::to_string(_peerID) + " could not get physical interface.");
        }
        else
        {
            std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>>::iterator channelIterator = valuesCentral.find(channel);
            if(channelIterator == valuesCentral.end()) return Variable::createError(-2, "Unknown channel.");
            std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator parameterIterator = channelIterator->second.find(parameter->id);
            if(parameterIterator == channelIterator->second.end()) return Variable::createError(-5, "Unknown parameter.");

            BaseLib::PArray parameters = std::make_shared<Array>();
            parameters->reserve(2);
            parameters->push_back(std::make_shared<Variable>(_serialNumber + ":" + std::to_string(channel)));
            parameters->push_back(std::make_shared<Variable>(parameter->id));
            auto result = interface->invoke(_rpcType, "getValue", parameters);
            if(result->errorStruct) return result;

            std::vector<uint8_t> parameterData;
            parameter->convertToPacket(result, parameterData);
            parameterIterator->second.setBinaryData(parameterData);
            if(parameterIterator->second.databaseId > 0) saveParameter(parameterIterator->second.databaseId, parameterData);
            else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, parameter->id, parameterData);

            return result;
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
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable MyPeer::getDeviceInfo(BaseLib::PRpcClientInfo clientInfo, std::map<std::string, bool> fields)
{
    try
    {
        PVariable info(Peer::getDeviceInfo(clientInfo, fields));
        if(info->errorStruct) return info;

        if(fields.empty() || fields.find("INTERFACE") != fields.end()) info->structValue->insert(StructElement("INTERFACE", PVariable(new Variable(_physicalInterfaceId))));

        return info;
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
    return PVariable();
}

PParameterGroup MyPeer::getParameterSet(int32_t channel, ParameterGroup::Type::Enum type)
{
    try
    {
        PFunction rpcChannel = _rpcDevice->functions.at(channel);
        if(type == ParameterGroup::Type::Enum::variables) return rpcChannel->variables;
        else if(type == ParameterGroup::Type::Enum::config) return rpcChannel->configParameters;
        else if(type == ParameterGroup::Type::Enum::link) return rpcChannel->linkParameters;
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
    return PParameterGroup();
}

bool MyPeer::getAllValuesHook2(PRpcClientInfo clientInfo, PParameter parameter, uint32_t channel, PVariable parameters)
{
    try
    {
        if(channel == 1)
        {
            if(parameter->id == "PEER_ID")
            {
                std::vector<uint8_t> parameterData;
                parameter->convertToPacket(PVariable(new Variable((int32_t)_peerID)), parameterData);
                valuesCentral[channel][parameter->id].setBinaryData(parameterData);
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
    return false;
}

bool MyPeer::getParamsetHook2(PRpcClientInfo clientInfo, PParameter parameter, uint32_t channel, PVariable parameters)
{
    try
    {
        if(channel == 1)
        {
            if(parameter->id == "PEER_ID")
            {
                std::vector<uint8_t> parameterData;
                parameter->convertToPacket(PVariable(new Variable((int32_t)_peerID)), parameterData);
                valuesCentral[channel][parameter->id].setBinaryData(parameterData);
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
    return false;
}

PVariable MyPeer::getParamset(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, bool checkAcls)
{
    try
    {
        if(_disposing) return Variable::createError(-32500, "Peer is disposing.");
        if(channel < 0) channel = 0;
        if(remoteChannel < 0) remoteChannel = 0;
        Functions::iterator functionIterator = _rpcDevice->functions.find(channel);
        if(functionIterator == _rpcDevice->functions.end()) return Variable::createError(-2, "Unknown channel");
        if(type == ParameterGroup::Type::none) type = ParameterGroup::Type::link;
        PParameterGroup parameterGroup = functionIterator->second->getParameterGroup(type);
        if(!parameterGroup) return Variable::createError(-3, "Unknown parameter set");

        auto central = getCentral();
        if(!central) return Variable::createError(-32500, "Could not get central.");

        auto interface = GD::interfaces->getInterface(_physicalInterfaceId);
        if(!interface)
        {
            GD::out.printError("Error: Peer " + std::to_string(_peerID) + " could not get physical interface.");
        }
        else
        {
            PArray parameters = std::make_shared<Array>();
            parameters->reserve(2);
            parameters->push_back(std::make_shared<Variable>(_serialNumber + (channel == 0 && type == ParameterGroup::Type::Enum::config ? "" : ":" + std::to_string(channel))));

            if(type == ParameterGroup::Type::link)
            {
                auto remotePeer = central->getPeer(remoteID);
                if(!remotePeer)
                {
                    GD::out.printError("Error: Could not find remote peer.");
                    return Variable::createError(-1, "Remote peer not found.");
                }

                parameters->push_back(std::make_shared<Variable>(remotePeer->getSerialNumber() + ":" + std::to_string(remoteChannel)));
            }
            else if(type == ParameterGroup::Type::variables) parameters->push_back(std::make_shared<Variable>(std::string("VALUES")));
            else parameters->push_back(std::make_shared<Variable>(std::string("MASTER")));
            auto paramset = interface->invoke(_rpcType, "getParamset", parameters);

            if(paramset->errorStruct) return paramset;

            if(type == ParameterGroup::Type::variables)
            {
                auto channelIterator = valuesCentral.find(channel);
                if(channelIterator != valuesCentral.end())
                {
                    for(auto& parameter : *paramset->structValue)
                    {
                        auto variableIterator = channelIterator->second.find(parameter.first);
                        if(variableIterator == channelIterator->second.end()) continue;

                        BaseLib::Systems::RpcConfigurationParameter& localParameter = variableIterator->second;
                        if(!localParameter.rpcParameter) continue;

                        std::vector<uint8_t> binaryValue;
                        localParameter.rpcParameter->convertToPacket(parameter.second, binaryValue);
                        localParameter.setBinaryData(binaryValue);
                        if(localParameter.databaseId > 0) saveParameter(localParameter.databaseId, binaryValue);
                        else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, parameter.first, binaryValue);
                    }
                }
            }
            else if(type == ParameterGroup::Type::config)
            {
                auto channelIterator = configCentral.find(channel);
                if(channelIterator != configCentral.end())
                {
                    for(auto& parameter : *paramset->structValue)
                    {
                        auto configIterator = channelIterator->second.find(parameter.first);
                        if(configIterator == channelIterator->second.end()) continue;

                        BaseLib::Systems::RpcConfigurationParameter& localParameter = configIterator->second;
                        if(!localParameter.rpcParameter) continue;

                        std::vector<uint8_t> binaryValue;
                        localParameter.rpcParameter->convertToPacket(parameter.second, binaryValue);
                        localParameter.setBinaryData(binaryValue);
                        if(localParameter.databaseId > 0) saveParameter(localParameter.databaseId, binaryValue);
                        else saveParameter(0, ParameterGroup::Type::Enum::config, channel, parameter.first, binaryValue);
                    }
                }
            }
            else if(type == ParameterGroup::Type::link)
            {
                auto channelIterator = linksCentral.find(channel);
                if(channelIterator != linksCentral.end())
                {
                    auto remotePeerIterator = channelIterator->second.find(remoteID);
                    if(remotePeerIterator != channelIterator->second.end())
                    {
                        auto remoteChannelIterator = remotePeerIterator->second.find(remoteChannel);
                        if(remoteChannelIterator != remotePeerIterator->second.end())
                        {
                            for(auto& parameter : *paramset->structValue)
                            {
                                auto configIterator = remoteChannelIterator->second.find(parameter.first);
                                if(configIterator == remoteChannelIterator->second.end()) continue;

                                BaseLib::Systems::RpcConfigurationParameter& localParameter = configIterator->second;
                                if(!localParameter.rpcParameter) continue;

                                std::vector<uint8_t> binaryValue;
                                localParameter.rpcParameter->convertToPacket(parameter.second, binaryValue);
                                localParameter.setBinaryData(binaryValue);
                                if(localParameter.databaseId > 0) saveParameter(localParameter.databaseId, binaryValue);
                                else saveParameter(0, ParameterGroup::Type::Enum::config, channel, parameter.first, binaryValue, remoteID, remoteChannel);
                            }
                        }
                    }
                }
            }

            return paramset;
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
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable MyPeer::putParamset(BaseLib::PRpcClientInfo clientInfo, int32_t channel, ParameterGroup::Type::Enum type, uint64_t remoteID, int32_t remoteChannel, PVariable variables, bool checkAcls, bool onlyPushing)
{
    try
    {
        if(_disposing) return Variable::createError(-32500, "Peer is disposing.");
        if(channel < 0) channel = 0;
        if(remoteChannel < 0) remoteChannel = 0;
        Functions::iterator functionIterator = _rpcDevice->functions.find(channel);
        if(functionIterator == _rpcDevice->functions.end()) return Variable::createError(-2, "Unknown channel.");
        if(type == ParameterGroup::Type::none) type = ParameterGroup::Type::link;
        PParameterGroup parameterGroup = functionIterator->second->getParameterGroup(type);
        if(!parameterGroup) return Variable::createError(-3, "Unknown parameter set.");
        if(variables->structValue->empty()) return PVariable(new Variable(VariableType::tVoid));

        auto central = getCentral();
        if(!central) return Variable::createError(-32500, "Could not get central.");

        bool parameterChanged = false;
        if(type == ParameterGroup::Type::Enum::config)
        {
            for(Struct::iterator i = variables->structValue->begin(); i != variables->structValue->end(); ++i)
            {
                if(i->first.empty() || !i->second) continue;
                if(configCentral[channel].find(i->first) == configCentral[channel].end()) continue;
                BaseLib::Systems::RpcConfigurationParameter& parameter = configCentral[channel][i->first];
                if(!parameter.rpcParameter) continue;
                if(parameter.rpcParameter->password && i->second->stringValue.empty()) continue; //Don't safe password if empty
                std::vector<uint8_t> parameterData;
                parameter.rpcParameter->convertToPacket(i->second, parameterData);
                parameter.setBinaryData(parameterData);
                if(parameter.databaseId > 0) saveParameter(parameter.databaseId, parameterData);
                else saveParameter(0, ParameterGroup::Type::Enum::config, channel, i->first, parameterData);

                parameterChanged = true;
                GD::out.printInfo("Info: Parameter " + i->first + " of peer " + std::to_string(_peerID) + " and channel " + std::to_string(channel) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(parameterData) + ".");
            }
        }
        else if(type == ParameterGroup::Type::Enum::variables)
        {
            for(Struct::iterator i = variables->structValue->begin(); i != variables->structValue->end(); ++i)
            {
                if(i->first.empty() || !i->second) continue;

                if(checkAcls && !clientInfo->acls->checkVariableWriteAccess(central->getPeer(_peerID), channel, i->first)) continue;

                setValue(clientInfo, channel, i->first, i->second, false);
            }

            return std::make_shared<BaseLib::Variable>();
        }

        auto interface = GD::interfaces->getInterface(_physicalInterfaceId);
        if(!interface)
        {
            GD::out.printError("Error: Peer " + std::to_string(_peerID) + " could not get physical interface.");
        }
        else
        {
            PArray parameters = std::make_shared<Array>();
            parameters->reserve(3);
            parameters->push_back(std::make_shared<Variable>(_serialNumber + (channel == 0 && type == ParameterGroup::Type::Enum::config ? "" : ":" + std::to_string(channel))));

            if(type == ParameterGroup::Type::link)
            {
                auto remotePeer = central->getPeer(remoteID);
                if(!remotePeer)
                {
                    GD::out.printError("Error: Could not find remote peer.");
                    return Variable::createError(-1, "Remote peer not found.");
                }

                parameters->push_back(std::make_shared<Variable>(remotePeer->getSerialNumber() + ":" + std::to_string(remoteChannel)));
            }
            else parameters->push_back(std::make_shared<Variable>(std::string("MASTER")));
            parameters->push_back(variables);
            auto result =  interface->invoke(_rpcType, "putParamset", parameters);

            if(parameterChanged) raiseRPCUpdateDevice(_peerID, channel, _serialNumber + ":" + std::to_string(channel), 0);

            return (result->type == BaseLib::VariableType::tString && result->stringValue.empty()) ? std::make_shared<BaseLib::Variable>() : result;
        }

        return std::make_shared<BaseLib::Variable>();
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
    return Variable::createError(-32500, "Unknown application error.");
}

void MyPeer::sendPacket(PMyPacket packet, std::string responseId, int32_t delay)
{
    try
    {
        _physicalInterface->sendPacket(packet);
        if(delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
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

PVariable MyPeer::setValue(BaseLib::PRpcClientInfo clientInfo, uint32_t channel, std::string valueKey, PVariable value, bool wait)
{
    try
    {
        if(_disposing) return Variable::createError(-32500, "Peer is disposing.");
        if(!value) return Variable::createError(-32500, "value is nullptr.");
        Peer::setValue(clientInfo, channel, valueKey, value, wait); //Ignore result, otherwise setHomegerValue might not be executed
        std::shared_ptr<MyCentral> central = std::dynamic_pointer_cast<MyCentral>(getCentral());
        if(!central) return Variable::createError(-32500, "Could not get central object.");;
        if(valueKey.empty()) return Variable::createError(-5, "Value key is empty.");
        if(channel == 0 && serviceMessages->set(valueKey, value->booleanValue)) return PVariable(new Variable(VariableType::tVoid));
        std::unordered_map<uint32_t, std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>>::iterator channelIterator = valuesCentral.find(channel);
        if(channelIterator == valuesCentral.end()) return Variable::createError(-2, "Unknown channel.");
        std::unordered_map<std::string, BaseLib::Systems::RpcConfigurationParameter>::iterator parameterIterator = channelIterator->second.find(valueKey);
        if(parameterIterator == valuesCentral[channel].end()) return Variable::createError(-5, "Unknown parameter.");
        PParameter rpcParameter = parameterIterator->second.rpcParameter;
        if(!rpcParameter) return Variable::createError(-5, "Unknown parameter.");
        BaseLib::Systems::RpcConfigurationParameter& parameter = valuesCentral[channel][valueKey];
        std::shared_ptr<std::vector<std::string>> valueKeys(new std::vector<std::string>());
        std::shared_ptr<std::vector<PVariable>> values(new std::vector<PVariable>());
        if(rpcParameter->readable)
        {
            valueKeys->push_back(valueKey);
            values->push_back(value);
        }
        if(rpcParameter->physical->operationType == IPhysical::OperationType::Enum::store)
        {
            std::vector<uint8_t> parameterData;
            rpcParameter->convertToPacket(value, parameterData);
            parameter.setBinaryData(parameterData);
            if(parameter.databaseId > 0) saveParameter(parameter.databaseId, parameterData);
            else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, valueKey, parameterData);
            if(!valueKeys->empty())
            {
                std::string address(_serialNumber + ":" + std::to_string(channel));
                raiseEvent(clientInfo->initInterfaceId, _peerID, channel, valueKeys, values);
                raiseRPCEvent(clientInfo->initInterfaceId, _peerID, channel, address, valueKeys, values);
            }
            return PVariable(new Variable(VariableType::tVoid));
        }
        else if(rpcParameter->physical->operationType != IPhysical::OperationType::Enum::command) return Variable::createError(-6, "Parameter is not settable.");
        if(rpcParameter->setPackets.empty() && !rpcParameter->writeable) return Variable::createError(-6, "parameter is read only");

        std::vector<uint8_t> parameterData;
        rpcParameter->convertToPacket(value, parameterData);
        parameter.setBinaryData(parameterData);
        if(parameter.databaseId > 0) saveParameter(parameter.databaseId, parameterData);
        else saveParameter(0, ParameterGroup::Type::Enum::variables, channel, valueKey, parameterData);
        if(_bl->debugLevel >= 4) GD::out.printInfo("Info: " + valueKey + " of peer " + std::to_string(_peerID) + " with serial number " + _serialNumber + ":" + std::to_string(channel) + " was set to 0x" + BaseLib::HelperFunctions::getHexString(parameterData) + ".");

        auto interface = GD::interfaces->getInterface(_physicalInterfaceId);
        if(!interface)
        {
            GD::out.printError("Error: Peer " + std::to_string(_peerID) + " could not get physical interface.");
        }
        else
        {
            if(value->type == BaseLib::VariableType::tInteger64) value->setType(BaseLib::VariableType::tInteger);

            PArray parameters = std::make_shared<Array>();
            parameters->reserve(3);
            parameters->push_back(std::make_shared<Variable>(_serialNumber + ":" + std::to_string(channel)));
            parameters->push_back(std::make_shared<Variable>(valueKey));
            parameters->push_back(value);
            auto result = interface->invoke(_rpcType, "setValue", parameters);
            if(result->errorStruct) GD::out.printError("Error: Could not execute setValue for peer " + std::to_string(_peerID) + ": " + result->structValue->at("faultString")->stringValue);
        }

        if(!valueKeys->empty())
        {
            std::string address(_serialNumber + ":" + std::to_string(channel));
            raiseEvent(clientInfo->initInterfaceId, _peerID, channel, valueKeys, values);
            raiseRPCEvent(clientInfo->initInterfaceId, _peerID, channel, address, valueKeys, values);
        }

        return PVariable(new Variable(VariableType::tVoid));
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
    return Variable::createError(-32500, "Unknown application error. See error log for more details.");
}

}
