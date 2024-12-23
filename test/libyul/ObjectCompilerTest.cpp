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

#include <test/libyul/ObjectCompilerTest.h>

#include <test/libhyperion/util/HyptestErrors.h>

#include <test/Common.h>

#include <libhyputil/AnsiColorized.h>

#include <libyul/YulStack.h>

#include <libzvmasm/Instruction.h>
#include <libzvmasm/Disassemble.h>

#include <liblangutil/DebugInfoSelection.h>
#include <liblangutil/SourceReferenceFormatter.h>

#include <boost/algorithm/string.hpp>

#include <fstream>

using namespace hyperion;
using namespace hyperion::util;
using namespace hyperion::langutil;
using namespace hyperion::yul;
using namespace hyperion::yul::test;
using namespace hyperion::frontend;
using namespace hyperion::frontend::test;
using namespace std;

ObjectCompilerTest::ObjectCompilerTest(string const& _filename):
	TestCase(_filename)
{
	m_source = m_reader.source();
	m_optimisationPreset = m_reader.enumSetting<OptimisationPreset>(
		"optimizationPreset",
		{
			{"none", OptimisationPreset::None},
			{"minimal", OptimisationPreset::Minimal},
			{"standard", OptimisationPreset::Standard},
			{"full", OptimisationPreset::Full},
		},
		"minimal"
	);
	m_expectation = m_reader.simpleExpectations();
}

TestCase::TestResult ObjectCompilerTest::run(ostream& _stream, string const& _linePrefix, bool const _formatted)
{
	YulStack stack(
		ZVMVersion(),
		YulStack::Language::StrictAssembly,
		OptimiserSettings::preset(m_optimisationPreset),
		DebugInfoSelection::All()
	);
	if (!stack.parseAndAnalyze("source", m_source))
	{
		AnsiColorized(_stream, _formatted, {formatting::BOLD, formatting::RED}) << _linePrefix << "Error parsing source." << endl;
		SourceReferenceFormatter{_stream, stack, true, false}
			.printErrorInformation(stack.errors());
		return TestResult::FatalError;
	}
	stack.optimize();

	MachineAssemblyObject obj = stack.assemble(YulStack::Machine::ZVM);
	hypAssert(obj.bytecode, "");
	hypAssert(obj.sourceMappings, "");

	m_obtainedResult = "Assembly:\n" + obj.assembly;
	if (obj.bytecode->bytecode.empty())
		m_obtainedResult += "-- empty bytecode --\n";
	else
		m_obtainedResult +=
			"Bytecode: " +
			util::toHex(obj.bytecode->bytecode) +
			"\nOpcodes: " +
			boost::trim_copy(zvmasm::disassemble(obj.bytecode->bytecode)) +
			"\nSourceMappings:" +
			(obj.sourceMappings->empty() ? "" : " " + *obj.sourceMappings) +
			"\n";

	return checkResult(_stream, _linePrefix, _formatted);
}
