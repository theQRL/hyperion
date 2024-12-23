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

#include <test/tools/ossfuzz/yulProto.pb.h>
#include <test/tools/ossfuzz/protoToYul.h>

#include <test/ZVMHost.h>

#include <test/tools/ossfuzz/YulZvmoneInterface.h>

#include <libyul/Exceptions.h>

#include <libyul/backends/zvm/ZVMCodeTransform.h>
#include <libyul/backends/zvm/ZVMDialect.h>

#include <libyul/optimiser/CallGraphGenerator.h>
#include <libyul/CompilabilityChecker.h>

#include <libzvmasm/Instruction.h>

#include <liblangutil/ZVMVersion.h>

#include <zvmone/zvmone.h>

#include <src/libfuzzer/libfuzzer_macro.h>

#include <fstream>

using namespace hyperion;
using namespace hyperion::test;
using namespace hyperion::test::fuzzer;
using namespace hyperion::yul;
using namespace hyperion::yul::test::yul_fuzzer;
using namespace hyperion::langutil;
using namespace std;

static zvmc::VM zvmone = zvmc::VM{zvmc_create_zvmone()};

namespace
{
/// @returns true if there are recursive functions, false otherwise.
bool recursiveFunctionExists(Dialect const& _dialect, yul::Object& _object)
{
	auto recursiveFunctions = CallGraphGenerator::callGraph(*_object.code).recursiveFunctions();
	for(auto&& [function, variables]: CompilabilityChecker{
			_dialect,
			_object,
			true
		}.unreachableVariables
	)
		if(recursiveFunctions.count(function))
			return true;
	return false;
}
}

DEFINE_PROTO_FUZZER(Program const& _input)
{
	// Hyperion creates an invalid instruction for subobjects, so we simply
	// ignore them in this fuzzer.
	if (_input.has_obj())
		return;
	bool filterStatefulInstructions = true;
	bool filterUnboundedLoops = true;
	ProtoConverter converter(
		filterStatefulInstructions,
		filterUnboundedLoops
	);
	string yul_source = converter.programToString(_input);
	// Do not fuzz the ZVM Version field.
	// See https://github.com/ethereum/solidity/issues/12590
	langutil::ZVMVersion version;
	ZVMHost hostContext(version, zvmone);
	hostContext.reset();

	if (const char* dump_path = getenv("PROTO_FUZZER_DUMP_PATH"))
	{
		ofstream of(dump_path);
		of.write(yul_source.data(), static_cast<streamsize>(yul_source.size()));
	}

	YulStringRepository::reset();

	hyperion::frontend::OptimiserSettings settings = hyperion::frontend::OptimiserSettings::full();
	settings.runYulOptimiser = false;
	settings.optimizeStackAllocation = false;
	bytes unoptimisedByteCode;
	bool recursiveFunction = false;
	bool unoptimizedStackTooDeep = false;
	try
	{
		YulAssembler assembler{version, nullopt, settings, yul_source};
		unoptimisedByteCode = assembler.assemble();
		auto yulObject = assembler.object();
		recursiveFunction = recursiveFunctionExists(
			ZVMDialect::strictAssemblyForZVMObjects(version),
			*yulObject
		);
	}
	catch (hyperion::yul::StackTooDeepError const&)
	{
		unoptimizedStackTooDeep = true;
	}

	ostringstream unoptimizedState;
	bool noRevertInSource = true;
	bool noInvalidInSource = true;
	if (!unoptimizedStackTooDeep)
	{
		zvmc::Result deployResult = YulZvmoneUtility{}.deployCode(unoptimisedByteCode, hostContext);
		if (deployResult.status_code != ZVMC_SUCCESS)
			return;
		auto callMessage = YulZvmoneUtility{}.callMessage(deployResult.create_address);
		zvmc::Result callResult = hostContext.call(callMessage);
		// If the fuzzer synthesized input does not contain the revert opcode which
		// we lazily check by string find, the ZVM call should not revert.
		noRevertInSource = yul_source.find("revert") == string::npos;
		noInvalidInSource = yul_source.find("invalid") == string::npos;
		if (noInvalidInSource)
			hypAssert(
				callResult.status_code != ZVMC_INVALID_INSTRUCTION,
				"Invalid instruction."
			);
		if (noRevertInSource)
			hypAssert(
				callResult.status_code != ZVMC_REVERT,
				"HyperionZvmoneInterface: ZVM One reverted"
			);
		// Bail out on serious errors encountered during a call.
		if (YulZvmoneUtility{}.seriousCallError(callResult.status_code))
			return;
		hypAssert(
			(callResult.status_code == ZVMC_SUCCESS ||
			(!noRevertInSource && callResult.status_code == ZVMC_REVERT) ||
			(!noInvalidInSource && callResult.status_code == ZVMC_INVALID_INSTRUCTION)),
			"Unoptimised call failed."
		);
		unoptimizedState << ZVMHostPrinter{hostContext, deployResult.create_address}.state();
	}

	settings.runYulOptimiser = true;
	settings.optimizeStackAllocation = true;
	bytes optimisedByteCode;
	try
	{
		optimisedByteCode = YulAssembler{version, nullopt, settings, yul_source}.assemble();
	}
	catch (hyperion::yul::StackTooDeepError const&)
	{
		if (!recursiveFunction)
			throw;
		else
			return;
	}

	if (unoptimizedStackTooDeep)
		return;
	// Reset host before running optimised code.
	hostContext.reset();
	zvmc::Result deployResultOpt = YulZvmoneUtility{}.deployCode(optimisedByteCode, hostContext);
	hypAssert(
		deployResultOpt.status_code == ZVMC_SUCCESS,
		"Zvmone: Optimized contract creation failed"
	);
	auto callMessageOpt = YulZvmoneUtility{}.callMessage(deployResultOpt.create_address);
	zvmc::Result callResultOpt = hostContext.call(callMessageOpt);
	if (noRevertInSource)
		hypAssert(
			callResultOpt.status_code != ZVMC_REVERT,
			"HyperionZvmoneInterface: ZVM One reverted"
		);
	if (noInvalidInSource)
		hypAssert(
			callResultOpt.status_code != ZVMC_INVALID_INSTRUCTION,
			"Invalid instruction."
		);
	hypAssert(
		(callResultOpt.status_code == ZVMC_SUCCESS ||
		 (!noRevertInSource && callResultOpt.status_code == ZVMC_REVERT) ||
		 (!noInvalidInSource && callResultOpt.status_code == ZVMC_INVALID_INSTRUCTION)),
		"Optimised call failed."
	);
	ostringstream optimizedState;
	optimizedState << ZVMHostPrinter{hostContext, deployResultOpt.create_address}.state();

	if (unoptimizedState.str() != optimizedState.str())
	{
		cout << unoptimizedState.str() << endl;
		cout << optimizedState.str() << endl;
		hypAssert(
			false,
			"State of unoptimised and optimised stack reused code do not match."
		);
	}
}
