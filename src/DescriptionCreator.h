/* Copyright 2013-2019 Homegear GmbH */

#ifndef HOMEGEAR_MBUS_DESCRIPTIONCREATOR_H
#define HOMEGEAR_MBUS_DESCRIPTIONCREATOR_H

#include <homegear-base/BaseLib.h>
#include "MyPacket.h"
#include "PhysicalInterfaces/Ccu.h"

#include <sys/stat.h>

namespace MyFamily
{

class DescriptionCreator
{
public:
    struct PeerInfo
    {
        std::string serialNumber;
        int32_t type = -1;
        int32_t firmwareVersion = 0x10;
    };

    DescriptionCreator();
    virtual ~DescriptionCreator() = default;

    DescriptionCreator::PeerInfo createDescription(Ccu::RpcType rpcType, std::string& interfaceId, std::string& serialNumber, uint32_t oldTypeNumber, std::unordered_set<uint32_t>& knownTypeNumbers);
private:
    std::string _xmlPath;

    void createDirectories();
    void addParameterSet(Ccu::RpcType rpcType, std::shared_ptr<Ccu>& interface, std::shared_ptr<HomegearDevice>& device, std::string& serialNumber, int32_t channel, std::string& type);
};

}

#endif
