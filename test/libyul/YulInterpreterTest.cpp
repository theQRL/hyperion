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

#include <test/libyul/YulInterpreterTest.h>

#include <test/tools/yulInterpreter/Interpreter.h>

#include <test/Common.h>

#include <libyul/backends/zvm/ZVMDialect.h>
#include <libyul/YulStack.h>
#include <libyul/AsmAnalysisInfo.h>

#include <liblangutil/DebugInfoSelection.h>
#include <liblangutil/ErrorReporter.h>
#include <liblangutil/SourceReferenceFormatter.h>

#include <libhyputil/AnsiColorized.h>

#include <boost/test/unit_test.hpp>
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

YulInterpreterTest::YulInterpreterTest(string const& _filename):
	ZVMVersionRestrictedTestCase(_filename)
{
	m_source = m_reader.source();
	m_expectation = m_reader.simpleExpectations();
	m_simulateExternalCallsToSelf = m_reader.boolSetting("simulateExternalCall", false);
}

TestCase::TestResult YulInterpreterTest::run(ostream& _stream, string const& _linePrefix, bool const _formatted)
{
	if (!parse(_stream, _linePrefix, _formatted))
		return TestResult::FatalError;

	m_obtainedResult = interpret();

	return checkResult(_stream, _linePrefix, _formatted);
}

bool YulInterpreterTest::parse(ostream& _stream, string const& _linePrefix, bool const _formatted)
{
	YulStack stack(
		hyperion::test::CommonOptions::get().zvmVersion(),
		YulStack::Language::StrictAssembly,
		hyperion::frontend::OptimiserSettings::none(),
		DebugInfoSelection::All()
	);
	if (stack.parseAndAnalyze("", m_source))
	{
		m_ast = stack.parserResult()->code;
		m_analysisInfo = stack.parserResult()->analysisInfo;
		return true;
	}
	else
	{
		AnsiColorized(_stream, _formatted, {formatting::BOLD, formatting::RED}) << _linePrefix << "Error parsing source." << endl;
		SourceReferenceFormatter{_stream, stack, true, false}
			.printErrorInformation(stack.errors());
		return false;
	}
}

string YulInterpreterTest::interpret()
{
	InterpreterState state;
	state.maxTraceSize = 32;
	state.maxSteps = 512;
	state.maxExprNesting = 64;
	try
	{
		Interpreter::run(
			state,
			ZVMDialect::strictAssemblyForZVMObjects(hyperion::test::CommonOptions::get().zvmVersion()),
			*m_ast,
			/*disableExternalCalls=*/ !m_simulateExternalCallsToSelf,
			/*disableMemoryTracing=*/ false
		);
	}
	catch (InterpreterTerminatedGeneric const&)
	{
	}

	stringstream result;
	state.dumpTraceAndState(result, false);
	return result.str();
}
