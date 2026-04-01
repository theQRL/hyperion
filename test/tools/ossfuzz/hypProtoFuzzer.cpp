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

#include <test/tools/ossfuzz/protoToHyp.h>
#include <test/tools/ossfuzz/HyperionQrvmoneInterface.h>
#include <test/tools/ossfuzz/hypProto.pb.h>

#include <test/QRVMHost.h>

#include <qrvmone/qrvmone.h>
#include <src/libfuzzer/libfuzzer_macro.h>

#include <fstream>

static qrvmc::VM qrvmone = qrvmc::VM{qrvmc_create_qrvmone()};

using namespace hyperion::test::fuzzer;
using namespace hyperion::test::hypprotofuzzer;
using namespace hyperion;
using namespace hyperion::frontend;
using namespace hyperion::test;
using namespace hyperion::util;
using namespace std;

DEFINE_PROTO_FUZZER(Program const& _input)
{
	ProtoConverter converter;
	string hyp_source = converter.protoToHyperion(_input);

	if (char const* dump_path = getenv("PROTO_FUZZER_DUMP_PATH"))
	{
		// With libFuzzer binary run this to generate a YUL source file x.yul:
		// PROTO_FUZZER_DUMP_PATH=x.yul ./a.out proto-input
		ofstream of(dump_path);
		of.write(hyp_source.data(), static_cast<streamsize>(hyp_source.size()));
	}

	if (char const* dump_path = getenv("HYP_DEBUG_FILE"))
	{
		hyp_source.clear();
		// With libFuzzer binary run this to generate a YUL source file x.yul:
		// PROTO_FUZZER_LOAD_PATH=x.yul ./a.out proto-input
		ifstream ifstr(dump_path);
		hyp_source = {
			std::istreambuf_iterator<char>(ifstr),
			std::istreambuf_iterator<char>()
		};
		std::cout << hyp_source << std::endl;
	}

	// We target the default QRVM which is the latest
	langutil::QRVMVersion version;
	QRVMHost hostContext(version, qrvmone);
	string contractName = "C";
	string libraryName = converter.libraryTest() ? converter.libraryName() : "";
	string methodName = "test()";
	StringMap source({{"test.hyp", hyp_source}});
	CompilerInput cInput(version, source, contractName, OptimiserSettings::minimal(), {});
	QrvmoneUtility qrvmoneUtil(
		hostContext,
		cInput,
		contractName,
		libraryName,
		methodName
	);
	auto minimalResult = qrvmoneUtil.compileDeployAndExecute();
	hypAssert(minimalResult.status_code != QRVMC_REVERT, "Hyp proto fuzzer: Qrvmone reverted.");
	if (minimalResult.status_code == QRVMC_SUCCESS)
		hypAssert(
			QrvmoneUtility::zeroWord(minimalResult.output_data, minimalResult.output_size),
			"Proto hypc fuzzer: Output incorrect"
		);
}
