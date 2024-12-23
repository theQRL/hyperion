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

#include <test/libyul/ZVMCodeTransformTest.h>
#include <test/libyul/Common.h>

#include <test/Common.h>

#include <libyul/YulStack.h>
#include <libyul/backends/zvm/ZondAssemblyAdapter.h>
#include <libyul/backends/zvm/ZVMObjectCompiler.h>

#include <libzvmasm/Assembly.h>

#include <liblangutil/SourceReferenceFormatter.h>

#include <libhyputil/AnsiColorized.h>

using namespace hyperion;
using namespace hyperion::util;
using namespace hyperion::langutil;
using namespace hyperion::yul;
using namespace hyperion::yul::test;
using namespace hyperion::frontend;
using namespace hyperion::frontend::test;
using namespace std;

ZVMCodeTransformTest::ZVMCodeTransformTest(string const& _filename):
	ZVMVersionRestrictedTestCase(_filename)
{
	m_source = m_reader.source();
	m_stackOpt = m_reader.boolSetting("stackOptimization", false);
	m_expectation = m_reader.simpleExpectations();
}

TestCase::TestResult ZVMCodeTransformTest::run(ostream& _stream, string const& _linePrefix, bool const _formatted)
{
	hyperion::frontend::OptimiserSettings settings = hyperion::frontend::OptimiserSettings::none();
	settings.runYulOptimiser = false;
	settings.optimizeStackAllocation = m_stackOpt;
	YulStack stack(
		ZVMVersion{},
		YulStack::Language::StrictAssembly,
		settings,
		DebugInfoSelection::All()
	);
	if (!stack.parseAndAnalyze("", m_source))
	{
		AnsiColorized(_stream, _formatted, {formatting::BOLD, formatting::RED}) << _linePrefix << "Error parsing source." << endl;
		SourceReferenceFormatter{_stream, stack, true, false}
			.printErrorInformation(stack.errors());
		return TestResult::FatalError;
	}

	zvmasm::Assembly assembly{hyperion::test::CommonOptions::get().zvmVersion(), false, {}};
	ZondAssemblyAdapter adapter(assembly);
	ZVMObjectCompiler::compile(
		*stack.parserResult(),
		adapter,
		ZVMDialect::strictAssemblyForZVMObjects(ZVMVersion{}),
		m_stackOpt
	);

	std::ostringstream output;
	output << assembly;
	m_obtainedResult = output.str();

	return checkResult(_stream, _linePrefix, _formatted);
}
