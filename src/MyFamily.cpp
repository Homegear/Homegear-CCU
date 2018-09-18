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

#include "GD.h"
#include "Interfaces.h"
#include "MyFamily.h"
#include "MyCentral.h"

namespace MyFamily
{

MyFamily::MyFamily(BaseLib::SharedObjects* bl, BaseLib::Systems::DeviceFamily::IFamilyEventSink* eventHandler) : BaseLib::Systems::DeviceFamily(bl, eventHandler, MY_FAMILY_ID, MY_FAMILY_NAME)
{
	GD::bl = bl;
	GD::family = this;
	GD::out.init(bl);
	GD::out.setPrefix(std::string("Module ") + MY_FAMILY_NAME + ": ");
	GD::out.printDebug("Debug: Loading module...");
    if(!enabled()) return;
	GD::interfaces = std::make_shared<Interfaces>(bl, _settings->getPhysicalInterfaceSettings());
    _physicalInterfaces = GD::interfaces;
}

MyFamily::~MyFamily()
{

}

bool MyFamily::init()
{
	_bl->out.printInfo("Loading XML RPC devices...");
	std::string xmlPath = _bl->settings.familyDataPath() + std::to_string(GD::family->getFamily()) + "/desc/";
	BaseLib::Io io;
	io.init(_bl);
	if(BaseLib::Io::directoryExists(xmlPath) && !io.getFiles(xmlPath).empty()) _rpcDevices->load(xmlPath);
	return true;
}

void MyFamily::dispose()
{
	if(_disposed) return;
	DeviceFamily::dispose();

	_central.reset();
}

void MyFamily::reloadRpcDevices()
{
    _bl->out.printInfo("Reloading XML RPC devices...");
    std::string xmlPath = _bl->settings.familyDataPath() + std::to_string(GD::family->getFamily()) + "/desc/";
    if(BaseLib::Io::directoryExists(xmlPath)) _rpcDevices->load(xmlPath);
}

void MyFamily::createCentral()
{
	try
	{
		_central.reset(new MyCentral(0, "VCCU20000001", this));
		GD::out.printMessage("Created central with id " + std::to_string(_central->getId()) + ".");
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

std::shared_ptr<BaseLib::Systems::ICentral> MyFamily::initializeCentral(uint32_t deviceId, int32_t address, std::string serialNumber)
{
	return std::shared_ptr<MyCentral>(new MyCentral(deviceId, serialNumber, this));
}

PVariable MyFamily::getPairingInfo()
{
	try
	{
		if(!_central) return std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
		PVariable info = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);

        //{{{ General
            info->structValue->emplace("searchInterfaces", std::make_shared<BaseLib::Variable>(true));
        //}}}

        //{{{ Pairing methods
            PVariable pairingMethods = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            pairingMethods->structValue->emplace("searchDevices", std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct));

            //{{{ setInstallMode
                PVariable setInstallModeMetadata = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
                PVariable setInstallModeMetadataInfo = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);

                setInstallModeMetadataInfo->structValue->emplace("interfaceSelector", std::make_shared<BaseLib::Variable>(true));

                PVariable typeSelector = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);

                PVariable typeSelectorBidcos = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
                typeSelectorBidcos->structValue->emplace("name", std::make_shared<BaseLib::Variable>("HomeMatic BidCoS"));
                typeSelectorBidcos->structValue->emplace("additionalFields", std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct));
                typeSelector->structValue->emplace("bidcos", typeSelectorBidcos);

                PVariable typeSelectorIp = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
                typeSelectorIp->structValue->emplace("name", std::make_shared<BaseLib::Variable>("HomeMatic IP"));
                typeSelectorIp->structValue->emplace("fieldsOptional", std::make_shared<BaseLib::Variable>(true));
                PVariable typeSelectorIpFields = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
                PVariable typeSelectorIpSgtin = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
                typeSelectorIpSgtin->structValue->emplace("name", std::make_shared<BaseLib::Variable>(std::string("l10n.ccu2.sgtin")));
                typeSelectorIpSgtin->structValue->emplace("description", std::make_shared<BaseLib::Variable>(std::string("l10n.ccu2.sgtindesc")));
                typeSelectorIpSgtin->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(0));
                typeSelectorIpSgtin->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
                typeSelectorIpFields->structValue->emplace("sgtin", typeSelectorIpSgtin);
                PVariable typeSelectorIpKey = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
                typeSelectorIpKey->structValue->emplace("name", std::make_shared<BaseLib::Variable>(std::string("l10n.ccu2.key")));
                typeSelectorIpKey->structValue->emplace("description", std::make_shared<BaseLib::Variable>(std::string("l10n.ccu2.keydesc")));
                typeSelectorIpKey->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(1));
                typeSelectorIpKey->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
                typeSelectorIpFields->structValue->emplace("key", typeSelectorIpKey);
                typeSelectorIp->structValue->emplace("additionalFields", typeSelectorIpFields);
                typeSelector->structValue->emplace("hmip", typeSelectorIp);

                setInstallModeMetadataInfo->structValue->emplace("typeSelector", typeSelector);
                setInstallModeMetadata->structValue->emplace("metadataInfo", setInstallModeMetadataInfo);

                pairingMethods->structValue->emplace("setInstallMode", setInstallModeMetadata);
                info->structValue->emplace("pairingMethods", pairingMethods);
            //}}}
        //}}}

        //{{{ interfaces
            PVariable interfaces = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);

            PVariable interface = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            interface->structValue->emplace("name", std::make_shared<BaseLib::Variable>(std::string("CCU2")));
            interface->structValue->emplace("ipDevice", std::make_shared<BaseLib::Variable>(true));

            PVariable field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(0));
            field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.id")));
            field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
            interface->structValue->emplace("id", field);

            field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(1));
            field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.serialNumber")));
            field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
            interface->structValue->emplace("serialnumber", field);

            field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            field->structValue->emplace("pos", std::make_shared<BaseLib::Variable>(2));
            field->structValue->emplace("label", std::make_shared<BaseLib::Variable>(std::string("l10n.common.hostname")));
            field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
            interface->structValue->emplace("host", field);

            field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
            field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(std::string("2001")));
            interface->structValue->emplace("port", field);

            field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
            field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(std::string("2010")));
            interface->structValue->emplace("port2", field);

            field = std::make_shared<BaseLib::Variable>(BaseLib::VariableType::tStruct);
            field->structValue->emplace("type", std::make_shared<BaseLib::Variable>(std::string("string")));
            field->structValue->emplace("const", std::make_shared<BaseLib::Variable>(std::string("2000")));
            interface->structValue->emplace("port3", field);

            interfaces->structValue->emplace("ccu2", interface);

            info->structValue->emplace("interfaces", interfaces);
        //}}}

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
	return Variable::createError(-32500, "Unknown application error.");
}
}
