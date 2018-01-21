/* Copyright 2013-2017 Homegear UG (haftungsbeschränkt) */

#include "DescriptionCreator.h"
#include "GD.h"

namespace MyFamily
{

DescriptionCreator::DescriptionCreator()
{

}

DescriptionCreator::PeerInfo DescriptionCreator::createDescription(Ccu2::RpcType rpcType, std::string& interfaceId, std::string& serialNumber, uint32_t oldTypeNumber, std::unordered_set<uint32_t>& knownTypeNumbers)
{
    try
    {
        createDirectories();

        uint32_t typeId = oldTypeNumber;
        while(typeId == 0 || knownTypeNumbers.find(typeId) != knownTypeNumbers.end()) typeId++;

        auto interface = GD::interfaces->getInterface(interfaceId);
        if(!interface) return PeerInfo();

        PArray parameters = std::make_shared<Array>();
        parameters->push_back(std::make_shared<Variable>(serialNumber));
        auto description = interface->invoke(rpcType, "getDeviceDescription", parameters);
        if(description->errorStruct)
        {
            GD::out.printError("Error: Could not call getDeviceDescription: " + description->structValue->at("faultString")->stringValue);
            return PeerInfo();
        }

        std::shared_ptr<HomegearDevice> device = std::make_shared<HomegearDevice>(GD::bl);
        auto descriptionIterator = description->structValue->find("VERSION");
        if(descriptionIterator != description->structValue->end()) device->version = descriptionIterator->second->integerValue;

        PSupportedDevice supportedDevice = std::make_shared<SupportedDevice>(GD::bl, device.get());
        descriptionIterator = description->structValue->find("TYPE");
        if(descriptionIterator != description->structValue->end()) supportedDevice->id = descriptionIterator->second->stringValue;
        if(supportedDevice->id.empty()) supportedDevice->id = serialNumber;
        supportedDevice->description = supportedDevice->id;
        supportedDevice->typeNumber = typeId;
        device->supportedDevices.push_back(supportedDevice);

        descriptionIterator = description->structValue->find("PARAMSETS");
        if(descriptionIterator != description->structValue->end())
        {
            for(auto& paramset : *descriptionIterator->second->arrayValue)
            {
                addParameterSet(rpcType, interface, device, serialNumber, -1, paramset->stringValue);
            }
        }

        descriptionIterator = description->structValue->find("CHILDREN");
        if(descriptionIterator != description->structValue->end())
        {
            for(auto& child : *descriptionIterator->second->arrayValue)
            {
                auto addressPair = BaseLib::HelperFunctions::splitFirst(child->stringValue, ':');
                int32_t channel = BaseLib::Math::getNumber(addressPair.second);

                parameters->at(0)->stringValue = child->stringValue;
                auto channelDescription = interface->invoke(rpcType, "getDeviceDescription", parameters);

                auto parametersetIterator = channelDescription->structValue->find("PARAMSETS");
                if(parametersetIterator != channelDescription->structValue->end())
                {
                    for(auto& paramset : *parametersetIterator->second->arrayValue)
                    {
                        addParameterSet(rpcType, interface, device, serialNumber, channel, paramset->stringValue);
                    }
                }
            }
        }

        std::string filename = _xmlPath + serialNumber + ".xml";
        device->save(filename);

        PeerInfo peerInfo;
        peerInfo.serialNumber = serialNumber;
        peerInfo.type = typeId;
        return peerInfo;
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

    return PeerInfo();
}

void DescriptionCreator::createDirectories()
{
    try
    {
        uid_t localUserId = GD::bl->hf.userId(GD::bl->settings.dataPathUser());
        gid_t localGroupId = GD::bl->hf.groupId(GD::bl->settings.dataPathGroup());
        if(((int32_t)localUserId) == -1 || ((int32_t)localGroupId) == -1)
        {
            localUserId = GD::bl->userId;
            localGroupId = GD::bl->groupId;
        }

        std::string path1 = GD::bl->settings.familyDataPath();
        std::string path2 = path1 + std::to_string(GD::family->getFamily()) + "/";
        _xmlPath = path2 + "desc/";
        if(!BaseLib::Io::directoryExists(path1)) BaseLib::Io::createDirectory(path1, GD::bl->settings.dataPathPermissions());
        if(localUserId != 0 || localGroupId != 0)
        {
            if(chown(path1.c_str(), localUserId, localGroupId) == -1) std::cerr << "Could not set owner on " << path1 << std::endl;
            if(chmod(path1.c_str(), GD::bl->settings.dataPathPermissions()) == -1) std::cerr << "Could not set permissions on " << path1 << std::endl;
        }
        if(!BaseLib::Io::directoryExists(path2)) BaseLib::Io::createDirectory(path2, GD::bl->settings.dataPathPermissions());
        if(localUserId != 0 || localGroupId != 0)
        {
            if(chown(path2.c_str(), localUserId, localGroupId) == -1) std::cerr << "Could not set owner on " << path2 << std::endl;
            if(chmod(path2.c_str(), GD::bl->settings.dataPathPermissions()) == -1) std::cerr << "Could not set permissions on " << path2 << std::endl;
        }
        if(!BaseLib::Io::directoryExists(_xmlPath)) BaseLib::Io::createDirectory(_xmlPath, GD::bl->settings.dataPathPermissions());
        if(localUserId != 0 || localGroupId != 0)
        {
            if(chown(_xmlPath.c_str(), localUserId, localGroupId) == -1) std::cerr << "Could not set owner on " << _xmlPath << std::endl;
            if(chmod(_xmlPath.c_str(), GD::bl->settings.dataPathPermissions()) == -1) std::cerr << "Could not set permissions on " << _xmlPath << std::endl;
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

void DescriptionCreator::addParameterSet(Ccu2::RpcType rpcType, std::shared_ptr<Ccu2>& interface, std::shared_ptr<HomegearDevice>& device, std::string& serialNumber, int32_t channel, std::string& type)
{
    try
    {
        if(type == "MASTER" && channel == 0) return;

        std::string methodName = "getParamsetId";
        PArray parameters = std::make_shared<Array>();
        parameters->reserve(2);
        parameters->push_back(std::make_shared<Variable>(channel == -1 ? serialNumber : serialNumber + ":" + std::to_string(channel)));
        parameters->push_back(std::make_shared<Variable>(type));
        auto paramsetId = interface->invoke(rpcType, methodName, parameters);
        if(paramsetId->errorStruct)
        {
            GD::out.printWarning("Warning: Could not call getParamsetId on channel " + std::to_string(channel));
            return;
        }

        methodName = "getParamsetDescription";
        auto parametersetDescription = interface->invoke(rpcType, methodName, parameters);
        if(parametersetDescription->errorStruct)
        {
            GD::out.printWarning("Warning: Could not call getParamsetDescription on channel " + std::to_string(channel));
            return;
        }

        auto functionIterator = device->functions.find(channel == -1 ? 0 : channel);
        PFunction function = functionIterator == device->functions.end() ? std::make_shared<Function>(GD::bl) : functionIterator->second;
        if(functionIterator == device->functions.end()) device->functions.emplace(channel == -1 ? 0 : channel, function);
        function->channel = channel == -1 ? 0 : channel;
        function->type = paramsetId->stringValue;
        if(type == "VALUES") function->variablesId = "CCU_" + type + (channel == -1 ? "" : "_" + std::to_string(channel));
        else if(type == "MASTER") function->configParametersId = "CCU_" + type + (channel == -1 ? "" : "_" + std::to_string(channel));
        else if(type == "LINK") function->linkParametersId = "CCU_" + type + (channel == -1 ? "" : "_" + std::to_string(channel));

        for(auto& parameterDescription : *parametersetDescription->structValue)
        {
            PParameter parameter = std::make_shared<Parameter>(GD::bl, function->variables.get());
            parameter->id = parameterDescription.first;

            parameter->casts.push_back(std::make_shared<BaseLib::DeviceDescription::ParameterCast::RpcBinary>(GD::bl));

            auto elementIterator = parameterDescription.second->structValue->find("TYPE");
            if(elementIterator == parameterDescription.second->structValue->end()) continue;
            std::string parameterType = elementIterator->second->stringValue;

            int32_t operations = 7;
            elementIterator = parameterDescription.second->structValue->find("OPERATIONS");
            if(elementIterator != parameterDescription.second->structValue->end()) operations = elementIterator->second->integerValue;

            int32_t flags = 1;
            elementIterator = parameterDescription.second->structValue->find("FLAGS");
            if(elementIterator != parameterDescription.second->structValue->end()) flags = elementIterator->second->integerValue;

            parameter->readable = (operations & 1) || (operations & 4);
            parameter->writeable = (operations & 2);

            if(flags & 2) parameter->internal = true;
            if(flags & 4) parameter->transform = true;
            if(flags & 8) parameter->service = true;
            if(flags & 16) parameter->sticky = true;

            parameter->physical = std::make_shared<PhysicalNone>(GD::bl);
            parameter->physical->operationType = IPhysical::OperationType::Enum::command;

            if(parameterType == "ACTION")
            {
                auto logical = std::make_shared<LogicalAction>(GD::bl);
                parameter->logical = logical;
            }
            else if(parameterType == "BOOL")
            {
                auto logical = std::make_shared<LogicalBoolean>(GD::bl);
                parameter->logical = logical;

                elementIterator = parameterDescription.second->structValue->find("DEFAULT");
                if(elementIterator != parameterDescription.second->structValue->end())
                {
                    logical->defaultValueExists = true;
                    logical->defaultValue = elementIterator->second->booleanValue;
                }
            }
            else if(parameterType == "INTEGER")
            {
                auto logical = std::make_shared<LogicalInteger>(GD::bl);
                parameter->logical = logical;

                elementIterator = parameterDescription.second->structValue->find("DEFAULT");
                if(elementIterator != parameterDescription.second->structValue->end())
                {
                    logical->defaultValueExists = true;
                    logical->defaultValue = elementIterator->second->integerValue;
                }

                elementIterator = parameterDescription.second->structValue->find("MIN");
                if(elementIterator != parameterDescription.second->structValue->end()) logical->minimumValue = elementIterator->second->integerValue;

                elementIterator = parameterDescription.second->structValue->find("MAX");
                if(elementIterator != parameterDescription.second->structValue->end()) logical->maximumValue = elementIterator->second->integerValue;

                elementIterator = parameterDescription.second->structValue->find("UNIT");
                if(elementIterator != parameterDescription.second->structValue->end()) parameter->unit = elementIterator->second->stringValue;

                elementIterator = parameterDescription.second->structValue->find("SPECIAL");
                if(elementIterator != parameterDescription.second->structValue->end())
                {
                    for(auto& special : *elementIterator->second->arrayValue)
                    {
                        auto idIterator = special->structValue->find("ID");
                        auto valueIterator = special->structValue->find("VALUE");

                        if(idIterator == special->structValue->end() || valueIterator == special->structValue->end()) continue;

                        logical->specialValuesStringMap[idIterator->second->stringValue] = valueIterator->second->integerValue;
                    }
                }
            }
            else if(parameterType == "ENUM")
            {
                auto logical = std::make_shared<LogicalEnumeration>(GD::bl);
                parameter->logical = logical;

                elementIterator = parameterDescription.second->structValue->find("DEFAULT");
                if(elementIterator != parameterDescription.second->structValue->end())
                {
                    logical->defaultValueExists = true;
                    logical->defaultValue = elementIterator->second->integerValue;
                }

                elementIterator = parameterDescription.second->structValue->find("MIN");
                if(elementIterator != parameterDescription.second->structValue->end()) logical->minimumValue = elementIterator->second->integerValue;

                elementIterator = parameterDescription.second->structValue->find("MAX");
                if(elementIterator != parameterDescription.second->structValue->end()) logical->maximumValue = elementIterator->second->integerValue;

                elementIterator = parameterDescription.second->structValue->find("UNIT");
                if(elementIterator != parameterDescription.second->structValue->end()) parameter->unit = elementIterator->second->stringValue;

                elementIterator = parameterDescription.second->structValue->find("VALUE_LIST");
                if(elementIterator != parameterDescription.second->structValue->end())
                {
                    int32_t index = 0;
                    logical->values.reserve(elementIterator->second->arrayValue->size());
                    for(auto& enumElement : *elementIterator->second->arrayValue)
                    {
                        BaseLib::DeviceDescription::EnumerationValue value;
                        value.id = enumElement->stringValue;
                        value.index = index;
                        logical->values.push_back(value);
                    }
                }
            }
            else if(parameterType == "FLOAT")
            {
                auto logical = std::make_shared<LogicalDecimal>(GD::bl);
                parameter->logical = logical;

                elementIterator = parameterDescription.second->structValue->find("DEFAULT");
                if(elementIterator != parameterDescription.second->structValue->end())
                {
                    logical->defaultValueExists = true;
                    logical->defaultValue = elementIterator->second->floatValue;
                }

                elementIterator = parameterDescription.second->structValue->find("MIN");
                if(elementIterator != parameterDescription.second->structValue->end()) logical->minimumValue = elementIterator->second->floatValue;

                elementIterator = parameterDescription.second->structValue->find("MAX");
                if(elementIterator != parameterDescription.second->structValue->end()) logical->maximumValue = elementIterator->second->floatValue;

                elementIterator = parameterDescription.second->structValue->find("UNIT");
                if(elementIterator != parameterDescription.second->structValue->end()) parameter->unit = elementIterator->second->stringValue;

                elementIterator = parameterDescription.second->structValue->find("SPECIAL");
                if(elementIterator != parameterDescription.second->structValue->end())
                {
                    for(auto& special : *elementIterator->second->arrayValue)
                    {
                        auto idIterator = special->structValue->find("ID");
                        auto valueIterator = special->structValue->find("VALUE");

                        if(idIterator == special->structValue->end() || valueIterator == special->structValue->end()) continue;

                        logical->specialValuesStringMap[idIterator->second->stringValue] = valueIterator->second->integerValue;
                    }
                }
            }
            else if(parameterType == "STRING")
            {
                parameter->logical = std::make_shared<LogicalString>(GD::bl);

                elementIterator = parameterDescription.second->structValue->find("UNIT");
                if(elementIterator != parameterDescription.second->structValue->end()) parameter->unit = elementIterator->second->stringValue;
            }

            if(type == "VALUES")
            {
                function->variables->parametersOrdered.push_back(parameter);
                function->variables->parameters[parameter->id] = parameter;
            }
            else if(type == "MASTER")
            {
                function->configParameters->parametersOrdered.push_back(parameter);
                function->configParameters->parameters[parameter->id] = parameter;
            }
            else if(type == "LINK")
            {
                function->linkParameters->parametersOrdered.push_back(parameter);
                function->linkParameters->parameters[parameter->id] = parameter;
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
