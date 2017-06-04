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
	_physicalInterfaces.reset(new Interfaces(bl, _settings->getPhysicalInterfaceSettings()));
}

MyFamily::~MyFamily()
{

}

void MyFamily::dispose()
{
	if(_disposed) return;
	DeviceFamily::dispose();

	_central.reset();
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

PVariable MyFamily::getPairingMethods()
{
	try
	{
		if(!_central) return PVariable(new Variable(VariableType::tArray));
		PVariable array(new Variable(VariableType::tArray));
		array->arrayValue->push_back(PVariable(new Variable(std::string("searchDevices"))));
		return array;
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
