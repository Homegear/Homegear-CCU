/* Copyright 2013-2017 Homegear UG (haftungsbeschränkt) */

#ifndef HOMEGEAR_MBUS_DESCRIPTIONCREATOR_H
#define HOMEGEAR_MBUS_DESCRIPTIONCREATOR_H

#include <homegear-base/BaseLib.h>
#include "MyPacket.h"
#include "PhysicalInterfaces/Ccu2.h"

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

    DescriptionCreator::PeerInfo createDescription(std::string& interfaceId, std::string& serialNumber, uint32_t oldTypeNumber, std::unordered_set<uint32_t>& knownTypeNumbers);
private:
    std::string _xmlPath;

    void createDirectories();
    void addParameterSet(std::shared_ptr<Ccu2>& interface, std::shared_ptr<HomegearDevice>& device, std::string& serialNumber, int32_t channel, std::string& type);
};

}

#endif
