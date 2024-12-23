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

#include <fstream>

#include <test/tools/ossfuzz/yulProto.pb.h>
#include <test/tools/ossfuzz/protoToYul.h>

#include <test/tools/fuzzer_common.h>

#include <test/libyul/YulOptimizerTestCommon.h>

#include <libyul/YulStack.h>
#include <libyul/Exceptions.h>

#include <libyul/backends/zvm/ZVMDialect.h>

#include <liblangutil/DebugInfoSelection.h>
#include <liblangutil/ZVMVersion.h>

#include <src/libfuzzer/libfuzzer_macro.h>

using namespace hyperion;
using namespace hyperion::langutil;
using namespace hyperion::yul;
using namespace hyperion::yul::test;
using namespace hyperion::yul::test::yul_fuzzer;
using namespace std;

DEFINE_PROTO_FUZZER(Program const& _input)
{
	ProtoConverter converter;
	string yul_source = converter.programToString(_input);
	ZVMVersion version = converter.version();

	if (const char* dump_path = getenv("PROTO_FUZZER_DUMP_PATH"))
	{
		// With libFuzzer binary run this to generate a YUL source file x.yul:
		// PROTO_FUZZER_DUMP_PATH=x.yul ./a.out proto-input
		ofstream of(dump_path);
		of.write(yul_source.data(), static_cast<streamsize>(yul_source.size()));
	}

	if (yul_source.size() > 1200)
		return;

	YulStringRepository::reset();

	// YulStack entry point
	YulStack stack(
		version,
		YulStack::Language::StrictAssembly,
		hyperion::frontend::OptimiserSettings::full(),
		DebugInfoSelection::All()
	);

	// Parse protobuf mutated YUL code
	if (
		!stack.parseAndAnalyze("source", yul_source) ||
		!stack.parserResult()->code ||
		!stack.parserResult()->analysisInfo ||
		Error::containsErrors(stack.errors())
	)
		yulAssert(false, "Proto fuzzer generated malformed program");

	// Optimize
	YulOptimizerTestCommon optimizerTest(
		stack.parserResult(),
		ZVMDialect::strictAssemblyForZVMObjects(version)
	);
	optimizerTest.setStep(optimizerTest.randomOptimiserStep(_input.step()));
	shared_ptr<hyperion::yul::Block> astBlock = optimizerTest.run();
	yulAssert(astBlock != nullptr, "Optimiser error.");
}
