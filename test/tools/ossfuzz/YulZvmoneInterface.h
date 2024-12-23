/*
    This file is part of hyperion.

    hyperion is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    hyperion is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with hyperion.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <test/ZVMHost.h>

#include <libyul/YulStack.h>

#include <libhyperion/interface/OptimiserSettings.h>

#include <liblangutil/DebugInfoSelection.h>

namespace hyperion::test::fuzzer
{
class YulAssembler
{
public:
	YulAssembler(
		langutil::ZVMVersion _zvmVersion,
		hyperion::frontend::OptimiserSettings _optSettings,
		std::string const& _yulSource
	):
		m_stack(
			_zvmVersion,
			hyperion::yul::YulStack::Language::StrictAssembly,
			_optSettings,
			langutil::DebugInfoSelection::All()
		),
		m_yulProgram(_yulSource),
		m_optimiseYul(_optSettings.runYulOptimiser)
	{}
	hyperion::bytes assemble();
	std::shared_ptr<yul::Object> object();
private:
	hyperion::yul::YulStack m_stack;
	std::string m_yulProgram;
	bool m_optimiseYul;
};

struct YulZvmoneUtility
{
	/// @returns the result of deploying bytecode @param _input on @param _host.
	static zvmc::Result deployCode(hyperion::bytes const& _input, ZVMHost& _host);
	/// @returns call message to be sent to @param _address.
	static zvmc_message callMessage(zvmc_address _address);
	/// @returns true if call result indicates a serious error, false otherwise.
	static bool seriousCallError(zvmc_status_code _code);
};
}
