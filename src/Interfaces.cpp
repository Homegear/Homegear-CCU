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

#include "Interfaces.h"
#include "GD.h"

namespace MyFamily
{

Interfaces::Interfaces(BaseLib::SharedObjects* bl, std::map<std::string, Systems::PPhysicalInterfaceSettings> physicalInterfaceSettings) : Systems::PhysicalInterfaces(bl, GD::family->getFamily(), physicalInterfaceSettings)
{
	create();
}

Interfaces::~Interfaces()
{
    _physicalInterfaces.clear();
    _defaultPhysicalInterface.reset();
}

void Interfaces::addEventHandlers(BaseLib::Systems::IPhysicalInterface::IPhysicalInterfaceEventSink* central)
{
    try
    {
        std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
        for(auto interface : _physicalInterfaces)
        {
            if(_physicalInterfaceEventhandlers.find(interface.first) != _physicalInterfaceEventhandlers.end()) continue;
            _physicalInterfaceEventhandlers[interface.first] = interface.second->addEventHandler(central);
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

void Interfaces::removeEventHandlers()
{
    try
    {
        std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
        for(auto interface : _physicalInterfaces)
        {
            auto physicalInterfaceEventhandler = _physicalInterfaceEventhandlers.find(interface.first);
            if(physicalInterfaceEventhandler == _physicalInterfaceEventhandlers.end()) continue;
            interface.second->removeEventHandler(physicalInterfaceEventhandler->second);
            _physicalInterfaceEventhandlers.erase(physicalInterfaceEventhandler);
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

std::vector<std::shared_ptr<Ccu>> Interfaces::getInterfaces()
{
    std::vector<std::shared_ptr<Ccu>> interfaces;
    try
    {
        std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
        for(auto interfaceBase : _physicalInterfaces)
        {
            std::shared_ptr<Ccu> interface(std::dynamic_pointer_cast<Ccu>(interfaceBase.second));
            if(!interface) continue;
            if(interface->isOpen()) interfaces.push_back(interface);
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return interfaces;
}

std::shared_ptr<Ccu> Interfaces::getDefaultInterface()
{
    std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
    return _defaultPhysicalInterface;
}

std::shared_ptr<Ccu> Interfaces::getInterface(std::string& name)
{
    std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
    auto interfaceBase = _physicalInterfaces.find(name);
    if(interfaceBase == _physicalInterfaces.end()) return std::shared_ptr<Ccu>();
    std::shared_ptr<Ccu> interface(std::dynamic_pointer_cast<Ccu>(interfaceBase->second));
    return interface;
}

std::shared_ptr<Ccu> Interfaces::getInterfaceByIp(std::string& ipAddress)
{
    std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
    for(auto& interfaceBase : _physicalInterfaces)
    {
        std::shared_ptr<Ccu> interface(std::dynamic_pointer_cast<Ccu>(interfaceBase.second));
        if(!interface) continue;
        if(interface->getHostname() == ipAddress) return interface;
    }
    return std::shared_ptr<Ccu>();
}

std::shared_ptr<Ccu> Interfaces::getInterfaceBySerial(std::string& serial)
{
    std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
    for(auto& interfaceBase : _physicalInterfaces)
    {
        std::shared_ptr<Ccu> interface(std::dynamic_pointer_cast<Ccu>(interfaceBase.second));
        if(!interface) continue;
        if(interface->getSerialNumber() == serial) return interface;
    }
    return std::shared_ptr<Ccu>();
}

void Interfaces::removeUnknownInterfaces(std::set<std::string>& knownInterfaces)
{
    try
    {
        std::vector<std::string> interfacesToDelete;
        std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
        for(auto& interfaceBase : _physicalInterfaces)
        {
            std::shared_ptr<Ccu> interface(std::dynamic_pointer_cast<Ccu>(interfaceBase.second));
            if(!interface) continue;
            if((interface->getType() != "ccu2-auto" && interface->getType() != "ccu-auto") || knownInterfaces.find(interfaceBase.first) != knownInterfaces.end() || interface->isOpen()) continue;
            {
                GD::out.printInfo("Removing CCU with serial number " + interfaceBase.first + " and IP address " + interface->getHostname() + ".");
                std::string name = interfaceBase.first + ".devicetype";
                GD::family->deleteFamilySettingFromDatabase(name);
                name = interfaceBase.first + ".host";
                GD::family->deleteFamilySettingFromDatabase(name);
                name = interfaceBase.first + ".serialnumber";
                GD::family->deleteFamilySettingFromDatabase(name);
                name = interfaceBase.first + ".port";
                GD::family->deleteFamilySettingFromDatabase(name);
                name = interfaceBase.first + ".port2";
                GD::family->deleteFamilySettingFromDatabase(name);
                name = interfaceBase.first + ".port3";
                GD::family->deleteFamilySettingFromDatabase(name);
                name = interfaceBase.first + ".port4";
                GD::family->deleteFamilySettingFromDatabase(name);

                interfacesToDelete.push_back(interfaceBase.first);
            }
        }

        for(auto& interface : interfacesToDelete)
        {
            _physicalInterfaces.erase(interface);
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

std::shared_ptr<Ccu> Interfaces::addInterface(Systems::PPhysicalInterfaceSettings settings, bool storeInDatabase)
{
    try
    {
        std::shared_ptr<Ccu> device;
        if(!settings || settings->type.empty()) return device;
        GD::out.printDebug("Debug: Creating physical device. Type is: " + settings->type);

        if(settings->type == "ccu2" || settings->type == "ccu" || settings->type == "ccu2-auto" || settings->type == "ccu-auto")
        {
            device = std::make_shared<Ccu>(settings);
        }
        else GD::out.printError("Error: Unsupported physical device type: " + settings->type);
        if(device)
        {
            std::lock_guard<std::mutex> interfaceGuard(_physicalInterfacesMutex);
            _physicalInterfaces[settings->id] = device;
            if(settings->isDefault || !_defaultPhysicalInterface || _defaultPhysicalInterface->getType() == "ccu-temp" || _defaultPhysicalInterface->getType() == "ccu2-temp") _defaultPhysicalInterface = device;
            if(storeInDatabase)
            {
                std::string name = settings->id + ".devicetype";
                GD::family->setFamilySetting(name, settings->type);
                name = settings->id + ".host";
                GD::family->setFamilySetting(name, settings->host);
                name = settings->id + ".serialnumber";
                GD::family->setFamilySetting(name, settings->serialNumber);
                name = settings->id + ".port";
                GD::family->setFamilySetting(name, settings->port);
                name = settings->id + ".port2";
                GD::family->setFamilySetting(name, settings->port2);
                name = settings->id + ".port3";
                GD::family->setFamilySetting(name, settings->port3);
                name = settings->id + ".port4";
                GD::family->setFamilySetting(name, settings->port4);
            }
        }
        return device;
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    return std::shared_ptr<Ccu>();
}

void Interfaces::create()
{
    try
    {
        for(auto& settings : _physicalInterfaceSettings)
        {
            if(settings.second->host.empty()) continue;
            addInterface(settings.second, false);
        }
        if(!_defaultPhysicalInterface)
        {
            Systems::PPhysicalInterfaceSettings settings = std::make_shared<Systems::PhysicalInterfaceSettings>();
            settings->type = "ccu-temp";
            _defaultPhysicalInterface = std::make_shared<Ccu>(settings);
        }
    }
    catch(const std::exception& ex)
    {
        GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
}

}
