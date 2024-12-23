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

#include <libyul/AsmAnalysis.h>
#include <libyul/AsmAnalysisInfo.h>

#include <liblangutil/ZVMVersion.h>
#include <liblangutil/Exceptions.h>

#include <test/libyul/Common.h>
#include <test/libyul/SyntaxTest.h>
#include <test/TestCaseReader.h>

#include <test/libhyperion/util/HyptestErrors.h>

#include <test/Common.h>

using namespace std;
using namespace hyperion;
using namespace hyperion::util;
using namespace hyperion::langutil;
using namespace hyperion::yul::test;
using namespace hyperion::frontend::test;

void SyntaxTest::parseAndAnalyze()
{
	if (m_sources.sources.size() != 1)
		BOOST_THROW_EXCEPTION(runtime_error{"Expected only one source for yul test."});

	string const& name = m_sources.sources.begin()->first;
	string const& source = m_sources.sources.begin()->second;

	ErrorList errorList{};
	hyptestAssert(m_dialect, "");
	// Silently ignoring the results.
	yul::test::parse(source, *m_dialect, errorList);
	for (auto const& error: errorList)
	{
		int locationStart = -1;
		int locationEnd = -1;

		if (SourceLocation const* location = error->sourceLocation())
		{
			locationStart = location->start;
			locationEnd = location->end;
		}

		m_errorList.emplace_back(SyntaxTestError{
			error->type(),
			error->errorId(),
			errorMessage(*error),
			name,
			locationStart,
			locationEnd
		});
	}

}

SyntaxTest::SyntaxTest(string const& _filename, langutil::ZVMVersion _zvmVersion):
	CommonSyntaxTest(_filename, _zvmVersion)
{
	string dialectName = m_reader.stringSetting("dialect", "zvmTyped");
	m_dialect = &dialect(dialectName, hyperion::test::CommonOptions::get().zvmVersion());
}
