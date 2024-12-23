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

#include <test/libyul/ControlFlowSideEffectsTest.h>

#include <test/Common.h>
#include <test/libyul/Common.h>

#include <libyul/Object.h>
#include <libyul/AST.h>
#include <libyul/ControlFlowSideEffects.h>
#include <libyul/ControlFlowSideEffectsCollector.h>
#include <libyul/backends/zvm/ZVMDialect.h>

using namespace std;
using namespace hyperion;
using namespace hyperion::yul;
using namespace hyperion::yul::test;
using namespace hyperion::frontend::test;

namespace
{
string toString(ControlFlowSideEffects const& _sideEffects)
{
	vector<string> r;
	if (_sideEffects.canTerminate)
		r.emplace_back("can terminate");
	if (_sideEffects.canRevert)
		r.emplace_back("can revert");
	if (_sideEffects.canContinue)
		r.emplace_back("can continue");
	return util::joinHumanReadable(r);
}
}

ControlFlowSideEffectsTest::ControlFlowSideEffectsTest(string const& _filename):
	TestCase(_filename)
{
	m_source = m_reader.source();
	m_expectation = m_reader.simpleExpectations();
}

TestCase::TestResult ControlFlowSideEffectsTest::run(ostream& _stream, string const& _linePrefix, bool _formatted)
{
	Object obj;
	std::tie(obj.code, obj.analysisInfo) = yul::test::parse(m_source, false);
	if (!obj.code)
		BOOST_THROW_EXCEPTION(runtime_error("Parsing input failed."));

	ControlFlowSideEffectsCollector sideEffects(
		ZVMDialect::strictAssemblyForZVMObjects(langutil::ZVMVersion()),
		*obj.code
	);
	m_obtainedResult.clear();
	forEach<FunctionDefinition const>(*obj.code, [&](FunctionDefinition const& _fun) {
		string effectStr = toString(sideEffects.functionSideEffects().at(&_fun));
		m_obtainedResult += _fun.name.str() + (effectStr.empty() ? ":" : ": " + effectStr) + "\n";
	});

	return checkResult(_stream, _linePrefix, _formatted);
}
