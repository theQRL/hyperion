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

#include <test/tools/ossfuzz/HyperionQrvmoneInterface.h>

#include <test/tools/ossfuzz/protoToAbiV2.h>

#include <src/libfuzzer/libfuzzer_macro.h>
#include <abicoder.hpp>

using namespace hyperion::frontend;
using namespace hyperion::test::fuzzer;
using namespace hyperion::test::abiv2fuzzer;
using namespace hyperion::test;
using namespace hyperion::util;
using namespace hyperion;
using namespace std;

static constexpr size_t abiCoderHeapSize = 1024 * 512;
static qrvmc::VM qrvmone = qrvmc::VM{qrvmc_create_qrvmone()};

DEFINE_PROTO_FUZZER(Contract const& _contract)
{
	ProtoConverter converter(_contract.seed());
	string contractSource = converter.contractToString(_contract);

	if (const char* dump_path = getenv("PROTO_FUZZER_DUMP_PATH"))
	{
		// With libFuzzer binary run this to generate the hyperion source file x.hyp from a proto input:
		// PROTO_FUZZER_DUMP_PATH=x.hyp ./a.out proto-input
		ofstream of(dump_path);
		of << contractSource;
	}

	string typeString = converter.isabelleTypeString();
	string valueString = converter.isabelleValueString();
	abicoder::ABICoder coder(abiCoderHeapSize);
	if (!typeString.empty() && converter.coderFunction())
	{
		auto [encodeStatus, encodedData] = coder.encode(typeString, valueString);
		hypAssert(encodeStatus, "Isabelle abicoder fuzzer: Encoding failed");

		// We target the default QRVM which is the latest
		langutil::QRVMVersion version;
		QRVMHost hostContext(version, qrvmone);
		string contractName = "C";
		StringMap source({{"test.hyp", contractSource}});
		CompilerInput cInput(version, source, contractName, OptimiserSettings::minimal(), {});
		QrvmoneUtility qrvmoneUtil(
			hostContext,
			cInput,
			contractName,
			{},
			{}
		);
		auto result = qrvmoneUtil.compileDeployAndExecute(encodedData);
		hypAssert(result.status_code != QRVMC_REVERT, "Proto ABIv2 fuzzer: QRVM One reverted.");
		if (result.status_code == QRVMC_SUCCESS)
			hypAssert(
				QrvmoneUtility::zeroWord(result.output_data, result.output_size),
				"Proto ABIv2 fuzzer: ABIv2 coding failure found."
			);
	}
}
