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
// SPDX-License-Identifier: GPL-3.0

#include <test/tools/ossfuzz/HyperionZvmoneInterface.h>
#include <test/tools/ossfuzz/protoToAbiV2.h>

#include <src/libfuzzer/libfuzzer_macro.h>

#include <fstream>

using namespace hyperion::frontend;
using namespace hyperion::test::fuzzer;
using namespace hyperion::test::abiv2fuzzer;
using namespace hyperion::test;
using namespace hyperion::util;
using namespace hyperion;
using namespace std;

static zvmc::VM zvmone = zvmc::VM{zvmc_create_zvmone()};

DEFINE_PROTO_FUZZER(Contract const& _input)
{
	string contract_source = ProtoConverter{_input.seed()}.contractToString(_input);

	if (const char* dump_path = getenv("PROTO_FUZZER_DUMP_PATH"))
	{
		// With libFuzzer binary run this to generate the hyperion source file x.hyp from a proto input:
		// PROTO_FUZZER_DUMP_PATH=x.hyp ./a.out proto-input
		ofstream of(dump_path);
		of << contract_source;
	}

	// We target the default ZVM which is the latest
	langutil::ZVMVersion version;
	ZVMHost hostContext(version, zvmone);
	string contractName = "C";
	string methodName = "test()";
	StringMap source({{"test.hyp", contract_source}});
	CompilerInput cInput(version, source, contractName, OptimiserSettings::minimal(), {});
	ZvmoneUtility zvmoneUtil(
		hostContext,
		cInput,
		contractName,
		{},
		methodName
	);
	// Invoke test function
	auto result = zvmoneUtil.compileDeployAndExecute();
	// We don't care about ZVM One failures other than ZVMC_REVERT
	hypAssert(result.status_code != ZVMC_REVERT, "Proto ABIv2 fuzzer: ZVM One reverted");
	if (result.status_code == ZVMC_SUCCESS)
		if (!ZvmoneUtility::zeroWord(result.output_data, result.output_size))
		{
			hyperion::bytes res;
			for (size_t i = 0; i < result.output_size; i++)
				res.push_back(result.output_data[i]);
			cout << hyperion::util::toHex(res) << endl;
			hypAssert(
				false,
				"Proto ABIv2 fuzzer: ABIv2 coding failure found"
			);
		}
}
