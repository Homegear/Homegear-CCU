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

#include "MyCentral.h"
#include "GD.h"

namespace MyFamily {

MyCentral::MyCentral(ICentralEventSink* eventHandler) : BaseLib::Systems::ICentral(MY_FAMILY_ID, GD::bl, eventHandler)
{
	init();
}

MyCentral::MyCentral(uint32_t deviceID, std::string serialNumber, ICentralEventSink* eventHandler) : BaseLib::Systems::ICentral(MY_FAMILY_ID, GD::bl, deviceID, serialNumber, -1, eventHandler)
{
	init();
}

MyCentral::~MyCentral()
{
	dispose();
}

void MyCentral::dispose(bool wait)
{
	try
	{
		if(_disposing) return;
		_disposing = true;

        {
            std::lock_guard<std::mutex> pairingModeGuard(_pairingModeThreadMutex);
            _stopPairingModeThread = true;
            _bl->threadManager.join(_pairingModeThread);
        }

		{
			std::lock_guard<std::mutex> searchDevicesGuard(_searchDevicesThreadMutex);
			_bl->threadManager.join(_searchDevicesThread);
		}

		GD::out.printDebug("Removing device " + std::to_string(_deviceId) + " from physical device's event queue...");
        GD::interfaces->removeEventHandlers();

        _stopWorkerThread = true;
		GD::out.printDebug("Debug: Waiting for worker thread of device " + std::to_string(_deviceId) + "...");
		_bl->threadManager.join(_workerThread);
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

void MyCentral::init()
{
	try
	{
		_shuttingDown = false;
		_stopWorkerThread = false;
        _pairing = false;
        _searching = false;

        GD::interfaces->addEventHandlers((BaseLib::Systems::IPhysicalInterface::IPhysicalInterfaceEventSink*)this);

		GD::bl->threadManager.start(_workerThread, true, _bl->settings.workerThreadPriority(), _bl->settings.workerThreadPolicy(), &MyCentral::worker, this);
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

void MyCentral::homegearShuttingDown()
{
	_shuttingDown = true;
}

void MyCentral::worker()
{
	try
	{
		while(GD::bl->booting && !_stopWorkerThread)
		{
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}

		std::chrono::milliseconds sleepingTime(1000);
		uint32_t counter = 0;
		uint32_t countsPer10Minutes = BaseLib::HelperFunctions::getRandomNumber(10, 600);

		BaseLib::PVariable metadata = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		metadata->structValue->emplace("addNewInterfaces", std::make_shared<BaseLib::Variable>(false));

		while(!_stopWorkerThread && !_shuttingDown)
		{
			try
			{
				std::this_thread::sleep_for(sleepingTime);
				if(_stopWorkerThread || _shuttingDown) return;
				// Update devices (most importantly the IP address)
				if(counter > countsPer10Minutes)
				{
					countsPer10Minutes = 600;
					counter = 0;
					searchInterfaces(nullptr, metadata);
				}
				counter++;
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

void MyCentral::loadPeers()
{
	try
	{
		std::shared_ptr<BaseLib::Database::DataTable> rows = _bl->db->getPeers(_deviceId);
		for(BaseLib::Database::DataTable::iterator row = rows->begin(); row != rows->end(); ++row)
		{
			int32_t peerID = row->second.at(0)->intValue;
			GD::out.printMessage("Loading CCU peer " + std::to_string(peerID));
			std::shared_ptr<MyPeer> peer(new MyPeer(peerID, row->second.at(2)->intValue, row->second.at(3)->textValue, _deviceId, this));
			if(!peer->load(this)) continue;
			if(!peer->getRpcDevice()) continue;
			std::lock_guard<std::mutex> peersGuard(_peersMutex);
			if(!peer->getSerialNumber().empty()) _peersBySerial[peer->getSerialNumber()] = peer;
			_peersById[peerID] = peer;
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

std::shared_ptr<MyPeer> MyCentral::getPeer(uint64_t id)
{
	try
	{
		std::lock_guard<std::mutex> peersGuard(_peersMutex);
		if(_peersById.find(id) != _peersById.end())
		{
			std::shared_ptr<MyPeer> peer(std::dynamic_pointer_cast<MyPeer>(_peersById.at(id)));
			return peer;
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
    return std::shared_ptr<MyPeer>();
}

std::shared_ptr<MyPeer> MyCentral::getPeer(std::string serialNumber)
{
	try
	{
		std::lock_guard<std::mutex> peersGuard(_peersMutex);
		if(_peersBySerial.find(serialNumber) != _peersBySerial.end())
		{
			std::shared_ptr<MyPeer> peer(std::dynamic_pointer_cast<MyPeer>(_peersBySerial.at(serialNumber)));
			return peer;
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
    return std::shared_ptr<MyPeer>();
}

bool MyCentral::onPacketReceived(std::string& senderId, std::shared_ptr<BaseLib::Systems::Packet> packet)
{
	try
	{
		if(_disposing) return false;
		PMyPacket myPacket(std::dynamic_pointer_cast<MyPacket>(packet));
		if(!myPacket) return false;

		if(_bl->debugLevel >= 4) std::cout << BaseLib::HelperFunctions::getTimeString(myPacket->timeReceived()) << " Packet received (" << senderId << "): Method name: " << myPacket->getMethodName() << std::endl;

        if(myPacket->getMethodName() == "newDevices")
        {
            if(_pairing)
            {
                auto parameters = myPacket->getParameters();
                if(parameters->size() < 2) return false;

                auto interface = GD::interfaces->getInterface(senderId);
                if(!interface) return false;
                auto deviceNames = interface->getNames();

                for(auto& description : *parameters->at(1)->arrayValue)
                {
                    auto addressIterator = description->structValue->find("ADDRESS");
                    if(addressIterator == description->structValue->end()) continue;
                    std::string serialNumber = addressIterator->second->stringValue;
                    BaseLib::HelperFunctions::stripNonAlphaNumeric(serialNumber);
                    if(serialNumber.find(':') != std::string::npos) continue;
                    std::string name;
                    auto deviceNameIterator = deviceNames.find(serialNumber);
                    if(deviceNameIterator != deviceNames.end()) name = deviceNameIterator->second;
                    pairDevice((Ccu::RpcType)parameters->at(0)->integerValue, senderId, serialNumber, name);
                }
                return true;
            }
            return false;
        }

        if(myPacket->getMethodName() == "event")
        {
            auto addressPair = BaseLib::HelperFunctions::splitFirst(myPacket->getParameters()->at(1)->stringValue, ':');
            std::string serialNumber = addressPair.first;

            PMyPeer peer = getPeer(serialNumber);
            if(!peer) return false;
            if(senderId != peer->getPhysicalInterfaceId()) return false;

            peer->packetReceived(myPacket);
            return true;
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

void MyCentral::pairDevice(Ccu::RpcType rpcType, std::string& interfaceId, std::string& serialNumber, std::string& name)
{
    try
    {
        std::lock_guard<std::mutex> pairGuard(_pairMutex);
        GD::out.printInfo("Info: Adding device " + serialNumber + "...");

        bool newPeer = true;
        auto peer = getPeer(serialNumber);
        std::unique_lock<std::mutex> lockGuard(_peersMutex);
        if(peer)
        {
            newPeer = false;
            if(_peersBySerial.find(peer->getSerialNumber()) != _peersBySerial.end()) _peersBySerial.erase(peer->getSerialNumber());
            if(_peersById.find(peer->getID()) != _peersById.end()) _peersById.erase(peer->getID());
			lockGuard.unlock();

			int32_t i = 0;
			while(peer.use_count() > 1 && i < 600)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				i++;
			}
			if(i == 600) GD::out.printError("Error: Peer deletion took too long.");
        }
        else lockGuard.unlock();


		auto knownTypeIds = GD::family->getRpcDevices()->getKnownTypeNumbers();
        auto peerInfo = _descriptionCreator.createDescription(rpcType, interfaceId, serialNumber, peer ? peer->getDeviceType() : 0, knownTypeIds);
        if(peerInfo.serialNumber.empty()) return; //Error
        GD::family->reloadRpcDevices();

        if(!peer)
        {
            peer = createPeer(peerInfo.type, peerInfo.firmwareVersion, peerInfo.serialNumber, true);
            if(!peer)
            {
                GD::out.printError("Error: Could not add device with type " + BaseLib::HelperFunctions::getHexString(peerInfo.type) + ". No matching XML file was found.");
                return;
            }

            peer->initializeCentralConfig();
            peer->setPhysicalInterfaceId(interfaceId);
            peer->setRpcType(rpcType);
            peer->setName(-1, name);
        }
        else
        {
            peer->setRpcDevice(GD::family->getRpcDevices()->find(peerInfo.type, peerInfo.firmwareVersion, -1));
            if(!peer->getRpcDevice())
            {
                GD::out.printError("Error: RPC device could not be found anymore.");
                return;
            }
            if(peer->getName(-1) == "") peer->setName(-1, name);
        }

        lockGuard.lock();
        _peersBySerial[peer->getSerialNumber()] = peer;
        _peersById[peer->getID()] = peer;
        lockGuard.unlock();

        if(newPeer)
        {
            GD::out.printInfo("Info: Device successfully added. Peer ID is: " + std::to_string(peer->getID()));

            PVariable deviceDescriptions(new Variable(VariableType::tArray));
            std::shared_ptr<std::vector<PVariable>> descriptions = peer->getDeviceDescriptions(nullptr, true, std::map<std::string, bool>());
            if(!descriptions) return;
            for(std::vector<PVariable>::iterator j = descriptions->begin(); j != descriptions->end(); ++j)
            {
                deviceDescriptions->arrayValue->push_back(*j);
            }
            std::vector<uint64_t> newIds{ peer->getID() };
            raiseRPCNewDevices(newIds, deviceDescriptions);
        }
        else
        {
            GD::out.printInfo("Info: Peer " + std::to_string(peer->getID()) + " successfully updated.");

            raiseRPCUpdateDevice(peer->getID(), 0, peer->getSerialNumber() + ":" + std::to_string(0), 0);
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

void MyCentral::savePeers(bool full)
{
	try
	{
		std::lock_guard<std::mutex> peersGuard(_peersMutex);
		for(std::map<uint64_t, std::shared_ptr<BaseLib::Systems::Peer>>::iterator i = _peersById.begin(); i != _peersById.end(); ++i)
		{
			GD::out.printInfo("Info: Saving CCU peer " + std::to_string(i->second->getID()));
			i->second->save(full, full, full);
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

void MyCentral::deletePeer(uint64_t id)
{
	try
	{
		std::shared_ptr<MyPeer> peer(getPeer(id));
		if(!peer) return;
		peer->deleting = true;
		PVariable deviceAddresses(new Variable(VariableType::tArray));
		deviceAddresses->arrayValue->push_back(PVariable(new Variable(peer->getSerialNumber())));

		PVariable deviceInfo(new Variable(VariableType::tStruct));
		deviceInfo->structValue->insert(StructElement("ID", PVariable(new Variable((int32_t)peer->getID()))));
		PVariable channels(new Variable(VariableType::tArray));
		deviceInfo->structValue->insert(StructElement("CHANNELS", channels));

		for(Functions::iterator i = peer->getRpcDevice()->functions.begin(); i != peer->getRpcDevice()->functions.end(); ++i)
		{
			deviceAddresses->arrayValue->push_back(PVariable(new Variable(peer->getSerialNumber() + ":" + std::to_string(i->first))));
			channels->arrayValue->push_back(PVariable(new Variable(i->first)));
		}

		{
			std::lock_guard<std::mutex> peersGuard(_peersMutex);
			if(_peersBySerial.find(peer->getSerialNumber()) != _peersBySerial.end()) _peersBySerial.erase(peer->getSerialNumber());
			if(_peersById.find(id) != _peersById.end()) _peersById.erase(id);
		}

        std::vector<uint64_t> deletedIds{ id };
		raiseRPCDeleteDevices(deletedIds, deviceAddresses, deviceInfo);

		int32_t i = 0;
		while(peer.use_count() > 1 && i < 600)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			i++;
		}
		if(i == 600) GD::out.printError("Error: Peer deletion took too long.");

		peer->deleteFromDatabase();

		GD::out.printMessage("Removed CCU peer " + std::to_string(peer->getID()));
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

std::string MyCentral::handleCliCommand(std::string command)
{
	try
	{
		std::ostringstream stringStream;
		std::vector<std::string> arguments;
		bool showHelp = false;
		if(BaseLib::HelperFunctions::checkCliCommand(command, "help", "h", "", 0, arguments, showHelp))
		{
			stringStream << "List of commands:" << std::endl << std::endl;
			stringStream << "For more information about the individual command type: COMMAND help" << std::endl << std::endl;
			stringStream << "search              Searches and adds CCUs" << std::endl;
			stringStream << "pairing on (pon)    Enables pairing mode" << std::endl;
			stringStream << "pairing off (pof)   Disables pairing mode" << std::endl;
			stringStream << "peers list (ls)     List all peers" << std::endl;
			stringStream << "peers remove (pr)   Remove a peer" << std::endl;
            stringStream << "peers reset (prs)   Unpair a peer and reset it to factory defaults" << std::endl;
            stringStream << "peers search        Searches for new peers" << std::endl;
			stringStream << "peers select (ps)   Select a peer" << std::endl;
			stringStream << "peers setname (pn)  Name a peer" << std::endl;
			stringStream << "unselect (u)        Unselect this device" << std::endl;
			return stringStream.str();
		}
		else if(BaseLib::HelperFunctions::checkCliCommand(command, "search", "", "", 0, arguments, showHelp))
		{
			if(showHelp)
			{
				stringStream << "Description: This command searches for and adds CCUs." << std::endl;
				stringStream << "Usage: search" << std::endl << std::endl;
				return stringStream.str();
			}

			searchInterfaces(nullptr, nullptr);

			stringStream << "Search completed." << std::endl;
			return stringStream.str();
		}
        else if(BaseLib::HelperFunctions::checkCliCommand(command, "peers search", "", "", 0, arguments, showHelp))
        {
            if(showHelp)
            {
                stringStream << "Description: This command searches for new peers paired to the connected CCUs." << std::endl;
                stringStream << "Usage: peers search" << std::endl << std::endl;
                return stringStream.str();
            }

            searchDevicesThread();

            stringStream << "Search completed." << std::endl;
            return stringStream.str();
        }
		else if(BaseLib::HelperFunctions::checkCliCommand(command, "pairing on", "pon", "", 0, arguments, showHelp))
		{
			if(showHelp)
			{
				stringStream << "Description: This command enables pairing mode." << std::endl;
				stringStream << "Usage: pairing on [DURATION]" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  DURATION: Optional duration in seconds to stay in pairing mode." << std::endl;
				return stringStream.str();
			}

			int32_t duration = 60;
			if(!arguments.empty())
			{
				duration = BaseLib::Math::getNumber(arguments.at(0), false);
				if(duration < 5 || duration > 3600) return "Invalid duration. Duration has to be greater than 5 and less than 3600.\n";
			}

			setInstallMode(nullptr, true, duration, nullptr, false);
			stringStream << "Pairing mode enabled." << std::endl;
			return stringStream.str();
		}
		else if(BaseLib::HelperFunctions::checkCliCommand(command, "pairing off", "pof", "", 0, arguments, showHelp))
		{
			if(showHelp)
			{
				stringStream << "Description: This command disables pairing mode." << std::endl;
				stringStream << "Usage: pairing off" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  There are no parameters." << std::endl;
				return stringStream.str();
			}

			setInstallMode(nullptr, false, -1, nullptr, false);
			stringStream << "Pairing mode disabled." << std::endl;
			return stringStream.str();
		}
		else if(BaseLib::HelperFunctions::checkCliCommand(command, "peers remove", "pr", "prm", 1, arguments, showHelp))
		{
			if(showHelp)
			{
				stringStream << "Description: This command removes a peer." << std::endl;
				stringStream << "Usage: peers remove PEERID" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  PEERID: The id of the peer to remove. Example: 513" << std::endl;
				return stringStream.str();
			}

			uint64_t peerID = BaseLib::Math::getNumber(arguments.at(0), false);
			if(peerID == 0) return "Invalid id.\n";

			if(!peerExists(peerID)) stringStream << "This peer is not paired to this central." << std::endl;
			else
			{
				stringStream << "Removing peer " << std::to_string(peerID) << std::endl;
				deletePeer(peerID);
			}
			return stringStream.str();
		}
        else if(BaseLib::HelperFunctions::checkCliCommand(command, "peers reset", "prs", "", 1, arguments, showHelp))
        {
            if(showHelp)
            {
                stringStream << "Description: This command unpairs a peer and resets it to factory defaults." << std::endl;
                stringStream << "Usage: peers reset PEERID" << std::endl << std::endl;
                stringStream << "Parameters:" << std::endl;
                stringStream << "  PEERID: The id of the peer to reset. Example: 513" << std::endl;
                return stringStream.str();
            }

            uint64_t peerID = BaseLib::Math::getNumber(arguments.at(0), false);
            if(peerID == 0) return "Invalid id.\n";

            if(!peerExists(peerID)) stringStream << "This peer is not paired to this central." << std::endl;
            else
            {
                stringStream << "Removing peer " << std::to_string(peerID) << std::endl;
                deleteDevice(nullptr, peerID, 1);
            }
            return stringStream.str();
        }
		else if(BaseLib::HelperFunctions::checkCliCommand(command, "peers list", "pl", "ls", 0, arguments, showHelp))
		{
			try
			{
				if(showHelp)
				{
					stringStream << "Description: This command lists information about all peers." << std::endl;
					stringStream << "Usage: peers list [FILTERTYPE] [FILTERVALUE]" << std::endl << std::endl;
					stringStream << "Parameters:" << std::endl;
					stringStream << "  FILTERTYPE:  See filter types below." << std::endl;
					stringStream << "  FILTERVALUE: Depends on the filter type. If a number is required, it has to be in hexadecimal format." << std::endl << std::endl;
					stringStream << "Filter types:" << std::endl;
					stringStream << "  ID: Filter by id." << std::endl;
					stringStream << "      FILTERVALUE: The id of the peer to filter (e. g. 513)." << std::endl;
					stringStream << "  SERIAL: Filter by serial number." << std::endl;
					stringStream << "      FILTERVALUE: The serial number of the peer to filter (e. g. JEQ0554309)." << std::endl;
					stringStream << "  NAME: Filter by name." << std::endl;
					stringStream << "      FILTERVALUE: The part of the name to search for (e. g. \"1st floor\")." << std::endl;
					stringStream << "  TYPE: Filter by device type." << std::endl;
					stringStream << "      FILTERVALUE: The 2 byte device type in hexadecimal format." << std::endl;
					return stringStream.str();
				}

				std::string filterType;
				std::string filterValue;

				if(arguments.size() >= 2)
				{
					 filterType = BaseLib::HelperFunctions::toLower(arguments.at(0));
					 filterValue = arguments.at(1);
					 if(filterType == "name") BaseLib::HelperFunctions::toLower(filterValue);
				}

				if(_peersById.empty())
				{
					stringStream << "No peers are paired to this central." << std::endl;
					return stringStream.str();
				}
				std::string bar(" │ ");
				const int32_t idWidth = 8;
				const int32_t nameWidth = 25;
				const int32_t serialWidth = 14;
				const int32_t typeWidth1 = 8;
				const int32_t typeWidth2 = 45;
				std::string nameHeader("Name");
				nameHeader.resize(nameWidth, ' ');
				std::string typeStringHeader("Type Description");
				typeStringHeader.resize(typeWidth2, ' ');
				stringStream << std::setfill(' ')
					<< std::setw(idWidth) << "ID" << bar
					<< nameHeader << bar
					<< std::setw(serialWidth) << "Serial Number" << bar
					<< std::setw(typeWidth1) << "Type" << bar
					<< typeStringHeader
					<< std::endl;
				stringStream << "─────────┼───────────────────────────┼────────────────┼──────────┼───────────────────────────────────────────────" << std::endl;
				stringStream << std::setfill(' ')
					<< std::setw(idWidth) << " " << bar
					<< std::setw(nameWidth) << " " << bar
					<< std::setw(serialWidth) << " " << bar
					<< std::setw(typeWidth1) << " " << bar
					<< std::setw(typeWidth2)
					<< std::endl;
				_peersMutex.lock();
				for(std::map<uint64_t, std::shared_ptr<BaseLib::Systems::Peer>>::iterator i = _peersById.begin(); i != _peersById.end(); ++i)
				{
					if(filterType == "id")
					{
						uint64_t id = BaseLib::Math::getNumber(filterValue, false);
						if(i->second->getID() != id) continue;
					}
					else if(filterType == "name")
					{
						std::string name = i->second->getName();
						if((signed)BaseLib::HelperFunctions::toLower(name).find(filterValue) == (signed)std::string::npos) continue;
					}
					else if(filterType == "serial")
					{
						if(i->second->getSerialNumber() != filterValue) continue;
					}
					else if(filterType == "type")
					{
						int32_t deviceType = BaseLib::Math::getNumber(filterValue, true);
						if((int32_t)i->second->getDeviceType() != deviceType) continue;
					}

					stringStream << std::setw(idWidth) << std::setfill(' ') << std::to_string(i->second->getID()) << bar;
					std::string name = i->second->getName();
					size_t nameSize = BaseLib::HelperFunctions::utf8StringSize(name);
					if(nameSize > (unsigned)nameWidth)
					{
						name = BaseLib::HelperFunctions::utf8Substring(name, 0, nameWidth - 3);
						name += "...";
					}
					else name.resize(nameWidth + (name.size() - nameSize), ' ');
					stringStream << name << bar
						<< std::setw(serialWidth) << i->second->getSerialNumber() << bar
						<< std::setw(typeWidth1) << BaseLib::HelperFunctions::getHexString(i->second->getDeviceType(), 6) << bar;
					if(i->second->getRpcDevice())
					{
						PSupportedDevice type = i->second->getRpcDevice()->getType(i->second->getDeviceType(), i->second->getFirmwareVersion());
						std::string typeID;
						if(type) typeID = type->description;
						if(typeID.size() > (unsigned)typeWidth2)
						{
							typeID.resize(typeWidth2 - 3);
							typeID += "...";
						}
						else typeID.resize(typeWidth2, ' ');
						stringStream << std::setw(typeWidth2) << typeID;
					}
					else stringStream << std::setw(typeWidth2);
					stringStream << std::endl << std::dec;
				}
				_peersMutex.unlock();
				stringStream << "─────────┴───────────────────────────┴────────────────┴──────────┴───────────────────────────────────────────────" << std::endl;

				return stringStream.str();
			}
			catch(const std::exception& ex)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(BaseLib::Exception& ex)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
			}
			catch(...)
			{
				_peersMutex.unlock();
				GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
			}
		}
		else if(command.compare(0, 13, "peers setname") == 0 || command.compare(0, 2, "pn") == 0)
		{
			uint64_t peerID = 0;
			std::string name;

			std::stringstream stream(command);
			std::string element;
			int32_t offset = (command.at(1) == 'n') ? 0 : 1;
			int32_t index = 0;
			while(std::getline(stream, element, ' '))
			{
				if(index < 1 + offset)
				{
					index++;
					continue;
				}
				else if(index == 1 + offset)
				{
					if(element == "help") break;
					else
					{
						peerID = BaseLib::Math::getNumber(element, false);
						if(peerID == 0) return "Invalid id.\n";
					}
				}
				else if(index == 2 + offset) name = element;
				else name += ' ' + element;
				index++;
			}
			if(index == 1 + offset)
			{
				stringStream << "Description: This command sets or changes the name of a peer to identify it more easily." << std::endl;
				stringStream << "Usage: peers setname PEERID NAME" << std::endl << std::endl;
				stringStream << "Parameters:" << std::endl;
				stringStream << "  PEERID:\tThe id of the peer to set the name for. Example: 513" << std::endl;
				stringStream << "  NAME:\tThe name to set. Example: \"1st floor light switch\"." << std::endl;
				return stringStream.str();
			}

			if(!peerExists(peerID)) stringStream << "This peer is not paired to this central." << std::endl;
			else
			{
				std::shared_ptr<MyPeer> peer = getPeer(peerID);
				peer->setName(name);
				stringStream << "Name set to \"" << name << "\"." << std::endl;
			}
			return stringStream.str();
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

std::shared_ptr<MyPeer> MyCentral::createPeer(uint32_t deviceType, int32_t firmwareVersion, std::string serialNumber, bool save)
{
    try
    {
        std::shared_ptr<MyPeer> peer(new MyPeer(_deviceId, this));
        peer->setDeviceType(deviceType);
        peer->setSerialNumber(serialNumber);
        peer->setRpcDevice(GD::family->getRpcDevices()->find(deviceType, firmwareVersion, -1));
        if(!peer->getRpcDevice()) return std::shared_ptr<MyPeer>();
        if(save) peer->save(true, true, false); //Save and create peerID
        return peer;
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
    return std::shared_ptr<MyPeer>();
}

PVariable MyCentral::deleteDevice(BaseLib::PRpcClientInfo clientInfo, std::string serialNumber, int32_t flags)
{
	try
	{
		if(serialNumber.empty()) return Variable::createError(-2, "Unknown device.");
		std::shared_ptr<MyPeer> peer = getPeer(serialNumber);
		if(!peer) return PVariable(new Variable(VariableType::tVoid));

        uint64_t peerId = peer->getID();
        peer.reset();

		return deleteDevice(clientInfo, peerId, flags);
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

PVariable MyCentral::deleteDevice(BaseLib::PRpcClientInfo clientInfo, uint64_t peerID, int32_t flags)
{
	try
	{
		if(peerID == 0) return Variable::createError(-2, "Unknown device.");
		std::shared_ptr<MyPeer> peer = getPeer(peerID);
		if(!peer) return PVariable(new Variable(VariableType::tVoid));
		uint64_t id = peer->getID();

        std::string interfaceId = peer->getPhysicalInterfaceId();
        auto interface = GD::interfaces->getInterface(interfaceId);
        if(interface && (flags & 8))
        {
            PArray parameters = std::make_shared<Array>();
            parameters->reserve(2);
            parameters->push_back(std::make_shared<Variable>(peer->getSerialNumber()));
            parameters->push_back(std::make_shared<Variable>(flags));
            auto result = interface->invoke(peer->getRpcType(), "deleteDevice", parameters);
            if(result->errorStruct) GD::out.printError("Error calling deleteDevice on CCU: " + result->structValue->at("faultString")->stringValue);
        }
        peer.reset();

		deletePeer(id);

		if(peerExists(id)) return Variable::createError(-1, "Error deleting peer. See log for more details.");

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
    return Variable::createError(-32500, "Unknown application error.");
}

PVariable MyCentral::getServiceMessages(PRpcClientInfo clientInfo, bool returnId, bool checkAcls)
{
    try
    {
        auto serviceMessages = ICentral::getServiceMessages(clientInfo, returnId, checkAcls);
        if(serviceMessages->errorStruct) return serviceMessages;

        auto interfaces = GD::interfaces->getInterfaces();
        for(auto& interface : interfaces)
        {
            auto ccuServiceMessages = interface->getServiceMessages();

			serviceMessages->arrayValue->reserve(serviceMessages->arrayValue->size() + ccuServiceMessages.size());

            for(auto& element : ccuServiceMessages)
            {
                auto peer = getPeer(element->serial);
                if(!peer) continue;

                if(returnId)
                {
                    auto newElement = std::make_shared<Variable>(VariableType::tStruct);
                    newElement->structValue->emplace("TYPE", std::make_shared<Variable>(2));
                    newElement->structValue->emplace("PEER_ID", std::make_shared<Variable>(peer->getID()));
                    newElement->structValue->emplace("TIMESTAMP", std::make_shared<Variable>(element->time));
                    newElement->structValue->emplace("MESSAGE", std::make_shared<Variable>(element->message));
                    newElement->structValue->emplace("VALUE", std::make_shared<Variable>(element->value));
                    serviceMessages->arrayValue->emplace_back(newElement);
                }
                else
				{
					auto newElement = std::make_shared<Variable>(VariableType::tArray);
					newElement->arrayValue->reserve(3);
					newElement->arrayValue->push_back(std::make_shared<BaseLib::Variable>(element->serial + ":0"));
					newElement->arrayValue->push_back(std::make_shared<BaseLib::Variable>(element->message));
					newElement->arrayValue->push_back(std::make_shared<BaseLib::Variable>(element->value));
					serviceMessages->arrayValue->emplace_back(newElement);
				}
            }
        }

        return serviceMessages;
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

void MyCentral::searchDevicesThread()
{
    try
    {
        auto interfaces = GD::interfaces->getInterfaces();
        for(auto& interface : interfaces)
        {
            auto deviceNames = interface->getNames();

            std::string methodName("listDevices");
            BaseLib::PArray parameters = std::make_shared<BaseLib::Array>();

            if(interface->hasBidCos())
            {
                auto result = interface->invoke(Ccu::RpcType::bidcos, methodName, parameters);
                if(result->errorStruct)
                {
                    GD::out.printWarning("Warning: Error calling listDevices for HomeMatic BidCoS on CCU " + interface->getID() + ": " + result->structValue->at("faultString")->stringValue);
                }
                else
                {
                    for(auto& description : *result->arrayValue)
                    {
                        auto addressIterator = description->structValue->find("ADDRESS");
                        if(addressIterator == description->structValue->end()) continue;
                        std::string serialNumber = addressIterator->second->stringValue;
                        BaseLib::HelperFunctions::stripNonAlphaNumeric(serialNumber);
                        if(serialNumber.find(':') != std::string::npos) continue;
                        std::string interfaceId = interface->getID();
                        std::string name;
                        auto deviceNameIterator = deviceNames.find(serialNumber);
                        if(deviceNameIterator != deviceNames.end()) name = deviceNameIterator->second;
                        pairDevice(Ccu::RpcType::bidcos, interfaceId, serialNumber, name);
                    }
                }
            }

            if(interface->hasHmip())
            {
                auto result = interface->invoke(Ccu::RpcType::hmip, methodName, parameters);
                if(result->errorStruct)
                {
                    GD::out.printWarning("Warning: Error calling listDevices for HomeMatic IP on CCU " + interface->getID() + ": " + result->structValue->at("faultString")->stringValue);
                }
                else
                {
                    for(auto& description : *result->arrayValue)
                    {
                        auto addressIterator = description->structValue->find("ADDRESS");
                        if(addressIterator == description->structValue->end()) continue;
                        std::string serialNumber = addressIterator->second->stringValue;
                        BaseLib::HelperFunctions::stripNonAlphaNumeric(serialNumber);
                        if(serialNumber.find(':') != std::string::npos) continue;
                        std::string interfaceId = interface->getID();
                        std::string name;
                        auto deviceNameIterator = deviceNames.find(serialNumber);
                        if(deviceNameIterator != deviceNames.end()) name = deviceNameIterator->second;
                        pairDevice(Ccu::RpcType::hmip, interfaceId, serialNumber, name);
                    }
                }
            }

            if(interface->hasWired())
            {
                methodName = "searchDevices";
                auto result = interface->invoke(Ccu::RpcType::wired, methodName, parameters);
                if(result->errorStruct)
                {
                    GD::out.printWarning("Warning: Error calling searchDevices for HomeMatic Wired on CCU " + interface->getID() + ": " + result->structValue->at("faultString")->stringValue);
                }
                else
                {
                    methodName = "listDevices";
                    result = interface->invoke(Ccu::RpcType::wired, methodName, parameters);
                    if(result->errorStruct)
                    {
                        GD::out.printWarning("Warning: Error calling listDevices for HomeMatic Wired on CCU " + interface->getID() + ": " + result->structValue->at("faultString")->stringValue);
                    }
                    else
                    {
                        for(auto& description : *result->arrayValue)
                        {
                            auto addressIterator = description->structValue->find("ADDRESS");
                            if(addressIterator == description->structValue->end()) continue;
                            std::string serialNumber = addressIterator->second->stringValue;
                            BaseLib::HelperFunctions::stripNonAlphaNumeric(serialNumber);
                            if(serialNumber.find(':') != std::string::npos) continue;
                            std::string interfaceId = interface->getID();
                            std::string name;
                            auto deviceNameIterator = deviceNames.find(serialNumber);
                            if(deviceNameIterator != deviceNames.end()) name = deviceNameIterator->second;
                            pairDevice(Ccu::RpcType::wired, interfaceId, serialNumber, name);
                        }
                    }
                }
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
    _searching = false;
}

PVariable MyCentral::searchDevices(BaseLib::PRpcClientInfo clientInfo)
{
	try
	{
        if(_searching) return std::make_shared<BaseLib::Variable>(0);
        _searching = true;
        std::lock_guard<std::mutex> searchDevicesGuard(_searchDevicesThreadMutex);
        _bl->threadManager.start(_searchDevicesThread, false, &MyCentral::searchDevicesThread, this);
        return std::make_shared<BaseLib::Variable>(-2);
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

PVariable MyCentral::searchInterfaces(BaseLib::PRpcClientInfo clientInfo, BaseLib::PVariable metadata)
{
	PFileDescriptor serverSocketDescriptor;
	try
	{
		bool addNewInterfaces = true;
		if(metadata)
		{
			auto metadataIterator = metadata->structValue->find("addNewInterfaces");
			if(metadataIterator != metadata->structValue->end()) addNewInterfaces = metadataIterator->second->booleanValue;
		}
        int32_t newInterfaceCount = 0;

        //{{{ UDP search
            serverSocketDescriptor = _bl->fileDescriptorManager.add(socket(AF_INET, SOCK_DGRAM, 0));
            if(serverSocketDescriptor->descriptor == -1)
            {
                _bl->out.printError("Error: Could not create socket.");
                return Variable::createError(-1, "Could not create socket.");
            }

            int32_t reuse = 1;
            if(setsockopt(serverSocketDescriptor->descriptor, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse)) == -1)
            {
                _bl->out.printWarning("Warning: Could set socket options: " + std::string(strerror(errno)));
            }

            char loopch = 0;
            if(setsockopt(serverSocketDescriptor->descriptor, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&loopch, sizeof(loopch)) == -1)
            {
                _bl->out.printWarning("Warning: Could set socket options: " + std::string(strerror(errno)));
            }

            struct in_addr localInterface;
            localInterface.s_addr = inet_addr("0.0.0.0");
            if(setsockopt(serverSocketDescriptor->descriptor, IPPROTO_IP, IP_MULTICAST_IF, (char *)&localInterface, sizeof(localInterface)) == -1)
            {
                _bl->out.printWarning("Warning: Could set socket options: " + std::string(strerror(errno)));
            }

            struct sockaddr_in localSock;
            memset((char *) &localSock, 0, sizeof(localSock));
            localSock.sin_family = AF_INET;
            localSock.sin_port = 0;
            localSock.sin_addr.s_addr = inet_addr("239.255.255.250");

            if(bind(serverSocketDescriptor->descriptor, (struct sockaddr*)&localSock, sizeof(localSock)) == -1)
            {
                _bl->out.printError("Error: Binding failed: " + std::string(strerror(errno)));
                _bl->fileDescriptorManager.close(serverSocketDescriptor);
                return Variable::createError(-2, "Binding failed.");
            }

            struct sockaddr_in addessInfo;
            addessInfo.sin_family = AF_INET;
            addessInfo.sin_addr.s_addr = inet_addr("239.255.255.250");
            addessInfo.sin_port = htons(43439);

            std::vector<uint8_t> broadcastPacket{2, 0xBE, 0x41, 0xD8, 1, 0x65, 0x51, 0x33, 0x2D, 0x2A, 0, 0x2A, 0, 0x49};
            if(sendto(serverSocketDescriptor->descriptor, (char*)broadcastPacket.data(), broadcastPacket.size(), 0, (struct sockaddr*)&addessInfo, sizeof(addessInfo)) == -1)
            {
                _bl->out.printWarning("Warning: Could send SSDP search broadcast packet: " + std::string(strerror(errno)));
            }

            uint64_t startTime = _bl->hf.getTime();
            char buffer[1024];
            int32_t bytesReceived = 0;
            struct sockaddr info{};
            socklen_t slen = sizeof(info);
            fd_set readFileDescriptor;
            timeval socketTimeout;
            int32_t nfds = 0;
            std::vector<uint8_t> expectedResponse{2, 0xBE, 0x41, 0xD8, 1};
            std::set<std::string> foundInterfaces;
            while(_bl->hf.getTime() - startTime <= 5000)
            {
                try
                {
                    if(!serverSocketDescriptor || serverSocketDescriptor->descriptor == -1) break;

                    socketTimeout.tv_sec = 0;
                    socketTimeout.tv_usec = 100000;
                    FD_ZERO(&readFileDescriptor);
                    auto fileDescriptorGuard = _bl->fileDescriptorManager.getLock();
                    fileDescriptorGuard.lock();
                    nfds = serverSocketDescriptor->descriptor + 1;
                    if(nfds <= 0)
                    {
                        fileDescriptorGuard.unlock();
                        _bl->out.printError("Error: Socket closed (1).");
                        _bl->fileDescriptorManager.shutdown(serverSocketDescriptor);
                        continue;
                    }
                    FD_SET(serverSocketDescriptor->descriptor, &readFileDescriptor);
                    fileDescriptorGuard.unlock();
                    bytesReceived = select(nfds, &readFileDescriptor, NULL, NULL, &socketTimeout);
                    if(bytesReceived == 0) continue;
                    if(bytesReceived != 1)
                    {
                        _bl->out.printError("Error: Socket closed (2).");
                        _bl->fileDescriptorManager.shutdown(serverSocketDescriptor);
                        continue;
                    }

                    bytesReceived = recvfrom(serverSocketDescriptor->descriptor, buffer, 1024, 0, &info, &slen);
                    if(bytesReceived == 0 || info.sa_family != AF_INET) continue;
                    else if(bytesReceived == -1)
                    {
                        _bl->out.printError("Error: Socket closed (3).");
                        _bl->fileDescriptorManager.shutdown(serverSocketDescriptor);
                        continue;
                    }
                    if(_bl->debugLevel >= 5) _bl->out.printDebug("Debug: Response received:\n" + std::string(buffer, bytesReceived));
                    std::vector<uint8_t> responseStart(buffer, buffer + 5);
                    if(responseStart == expectedResponse)
                    {
                        char* pos = (char*)memchr(buffer + 5, 0, bytesReceived - 5);
                        if(pos)
                        {
                            std::string device(buffer + 5, pos - (buffer + 5));
                            if(device == "eQ3-HM-CCU2-App")
                            {
                                pos = (char*)memchr(buffer + 5 + device.size() + 1, 0, bytesReceived - 5 - device.size() - 1);
                                if(pos)
                                {
                                    std::string serial(buffer + 5 + device.size() + 1, pos - (buffer + 5 + device.size() + 1));
                                    serial = BaseLib::HelperFunctions::stripNonAlphaNumeric(serial);

                                    pos = (char*)memchr(buffer + 5 + device.size() + 1 + serial.size() + 4, 0, bytesReceived - 5 - device.size() - 1 - serial.size() - 4);
                                    if(pos)
                                    {
                                        std::string version(buffer + 5 + device.size() + 1 + serial.size() + 4, pos - (buffer + 5 + device.size() + 1 + serial.size() + 4));

                                        struct sockaddr_in *s = (struct sockaddr_in*)&info;
                                        char ipStringBuffer[INET6_ADDRSTRLEN];
                                        inet_ntop(AF_INET, &s->sin_addr, ipStringBuffer, sizeof(ipStringBuffer));
                                        std::string senderIp(ipStringBuffer);

                                        if(serial.empty() || senderIp.empty()) continue;
                                        auto interface = GD::interfaces->getInterfaceBySerial(serial);
                                        if(interface && interface->getHostname() == senderIp) continue;

                                        if(!interface) newInterfaceCount++;

                                        Systems::PPhysicalInterfaceSettings settings = std::make_shared<Systems::PhysicalInterfaceSettings>();
                                        settings->id = serial;
                                        if(interface)
                                        {
                                            settings->type = interface->getType();
                                            settings->host = senderIp;
                                            settings->serialNumber = serial;
                                            settings->port = interface->getPort1();
                                            settings->port2 = interface->getPort2();
                                            settings->port3 = interface->getPort3();
                                        }
                                        else if(addNewInterfaces)
                                        {
                                            settings->type = "ccu-auto";
                                            settings->host = senderIp;
                                            settings->serialNumber = serial;
                                            settings->port = "2001";
                                            settings->port2 = "2010";
                                            settings->port3 = "2000";
                                        }

										if(interface || addNewInterfaces)
										{
											foundInterfaces.emplace(serial);

											std::shared_ptr<Ccu> newInterface = GD::interfaces->addInterface(settings, true);
											if(newInterface)
											{
												GD::out.printInfo("Info: Found new CCU with IP address " + senderIp + " and serial number " + settings->id + ".");
												newInterface->startListening();
											}
										}
                                    }
                                }
                            }
                        }
                    }
                }
                catch(const std::exception& ex)
                {
                    _bl->out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
                }
                catch(Exception& ex)
                {
                    _bl->out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
                }
                catch(...)
                {
                    _bl->out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
                }
            }

            _bl->fileDescriptorManager.shutdown(serverSocketDescriptor);
        //}}}

        if(!foundInterfaces.empty()) GD::interfaces->addEventHandlers((BaseLib::Systems::IPhysicalInterface::IPhysicalInterfaceEventSink*)this);
        if(addNewInterfaces) GD::interfaces->removeUnknownInterfaces(foundInterfaces);

        return std::make_shared<Variable>(newInterfaceCount);
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
	_bl->fileDescriptorManager.shutdown(serverSocketDescriptor);
	return Variable::createError(-32500, "Unknown application error.");
}

void MyCentral::pairingModeTimer(int32_t duration, bool debugOutput)
{
    try
    {
        _pairing = true;
        if(debugOutput) GD::out.printInfo("Info: Pairing mode enabled.");
        _timeLeftInPairingMode = duration;
        int64_t startTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        int64_t timePassed = 0;
        while(timePassed < ((int64_t)duration * 1000) && !_stopPairingModeThread)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            timePassed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - startTime;
            _timeLeftInPairingMode = duration - (timePassed / 1000);
        }
        _timeLeftInPairingMode = 0;
        _pairing = false;
        if(debugOutput) GD::out.printInfo("Info: Pairing mode disabled.");
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

std::shared_ptr<Variable> MyCentral::setInstallMode(BaseLib::PRpcClientInfo clientInfo, bool on, uint32_t duration, BaseLib::PVariable metadata, bool debugOutput)
{
    try
    {
        std::lock_guard<std::mutex> pairingModeGuard(_pairingModeThreadMutex);
        if(_disposing) return Variable::createError(-32500, "Central is disposing.");
        _stopPairingModeThread = true;
        _bl->threadManager.join(_pairingModeThread);
        _stopPairingModeThread = false;
        _timeLeftInPairingMode = 0;

        std::string ccu;
        std::string sgtin;
        std::string key;

        Ccu::RpcType rpcType = Ccu::RpcType::bidcos;
        if(metadata)
        {
            auto metadataIterator = metadata->structValue->find("interface");
            if(metadataIterator != metadata->structValue->end()) ccu = metadataIterator->second->stringValue;
            metadataIterator = metadata->structValue->find("type");
            if(metadataIterator != metadata->structValue->end() && metadataIterator->second->stringValue == "hmip") rpcType = Ccu::RpcType::hmip;
            metadataIterator = metadata->structValue->find("sgtin");
            if(metadataIterator != metadata->structValue->end()) sgtin = metadataIterator->second->stringValue;
            metadataIterator = metadata->structValue->find("key");
            if(metadataIterator != metadata->structValue->end()) key = metadataIterator->second->stringValue;
        }

        std::shared_ptr<Ccu> interface;
        if(!ccu.empty()) interface = GD::interfaces->getInterface(ccu);
        if(!interface) interface = GD::interfaces->getDefaultInterface();
        if(interface)
        {
            if(sgtin.empty() || key.empty())
            {
                std::string methodName("setInstallMode");
                PArray parameters = std::make_shared<Array>();
                parameters->reserve(3);
                parameters->push_back(std::make_shared<Variable>(on));
                parameters->push_back(std::make_shared<Variable>(duration));
                if(rpcType == Ccu::RpcType::bidcos) parameters->push_back(std::make_shared<Variable>(1));
                auto result = interface->invoke(rpcType, methodName, parameters);
                if(result->errorStruct)
                {
                    GD::out.printWarning("Warning: Could not call setInstallMode on default CCU: " + result->structValue->at("faultString")->stringValue);
                    return Variable::createError(-1, "Could not enable install mode. See log for more details.");
                }
            }
            else
            {
                std::string methodName("setInstallModeWithWhitelist");
                PArray parameters = std::make_shared<Array>();
                parameters->reserve(3);
                parameters->push_back(std::make_shared<Variable>(on));
                parameters->push_back(std::make_shared<Variable>(duration));
                BaseLib::PVariable whitelistStructs = std::make_shared<Variable>(BaseLib::VariableType::tArray);
                BaseLib::PVariable whitelistStruct = std::make_shared<Variable>(BaseLib::VariableType::tStruct);
                whitelistStructs->arrayValue->push_back(whitelistStruct);
                whitelistStruct->structValue->emplace("ADDRESS", std::make_shared<Variable>(sgtin));
				whitelistStruct->structValue->emplace("KEY", std::make_shared<Variable>(key));
                whitelistStruct->structValue->emplace("KEY_MODE", std::make_shared<Variable>("LOCAL"));
                parameters->push_back(whitelistStructs);
                auto result = interface->invoke(rpcType, methodName, parameters);
                if(result->errorStruct)
                {
                    GD::out.printWarning("Warning: Could not call setInstallModeWithWhitelist on default CCU: " + result->structValue->at("faultString")->stringValue);
                    return Variable::createError(-1, "Could not enable install mode. See log for more details.");
                }
            }
        }
        if(on && duration >= 5)
        {
            _timeLeftInPairingMode = duration; //It's important to set it here, because the thread often doesn't completely initialize before getInstallMode requests _timeLeftInPairingMode
            _bl->threadManager.start(_pairingModeThread, false, &MyCentral::pairingModeTimer, this, duration, debugOutput);
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
    return Variable::createError(-32500, "Unknown application error.");
}

}
