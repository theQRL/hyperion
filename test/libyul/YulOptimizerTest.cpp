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

#include <test/libyul/YulOptimizerTest.h>
#include <test/libyul/YulOptimizerTestCommon.h>

#include <test/libhyperion/util/HyptestErrors.h>
#include <test/libyul/Common.h>
#include <test/Common.h>

#include <libyul/Object.h>
#include <libyul/AsmPrinter.h>

#include <liblangutil/CharStreamProvider.h>
#include <liblangutil/SourceReferenceFormatter.h>
#include <liblangutil/Scanner.h>

#include <libhyputil/AnsiColorized.h>
#include <libhyputil/StringUtils.h>

#include <fstream>

using namespace hyperion;
using namespace hyperion::util;
using namespace hyperion::langutil;
using namespace hyperion::yul;
using namespace hyperion::yul::test;
using namespace hyperion::frontend;
using namespace hyperion::frontend::test;
using namespace std;

YulOptimizerTest::YulOptimizerTest(string const& _filename):
	ZVMVersionRestrictedTestCase(_filename)
{
	boost::filesystem::path path(_filename);

	if (path.empty() || std::next(path.begin()) == path.end() || std::next(std::next(path.begin())) == path.end())
		BOOST_THROW_EXCEPTION(runtime_error("Filename path has to contain a directory: \"" + _filename + "\"."));
	m_optimizerStep = std::prev(std::prev(path.end()))->string();

	m_source = m_reader.source();

	auto dialectName = m_reader.stringSetting("dialect", "zvm");
	m_dialect = &dialect(dialectName, hyperion::test::CommonOptions::get().zvmVersion());

	m_expectation = m_reader.simpleExpectations();
}

TestCase::TestResult YulOptimizerTest::run(ostream& _stream, string const& _linePrefix, bool const _formatted)
{
	std::tie(m_object, m_analysisInfo) = parse(_stream, _linePrefix, _formatted, m_source);
	if (!m_object)
		return TestResult::FatalError;

	hyptestAssert(m_dialect, "Dialect not set.");

	m_object->analysisInfo = m_analysisInfo;
	YulOptimizerTestCommon tester(m_object, *m_dialect);
	tester.setStep(m_optimizerStep);

	if (!tester.runStep())
	{
		AnsiColorized(_stream, _formatted, {formatting::BOLD, formatting::RED}) << _linePrefix << "Invalid optimizer step: " << m_optimizerStep << endl;
		return TestResult::FatalError;
	}

	auto const printed = (m_object->subObjects.empty() ? AsmPrinter{ *m_dialect }(*m_object->code) : m_object->toString(m_dialect));

	// Re-parse new code for compilability
	// TODO: support for wordSizeTransform which needs different input and output dialects
	if (m_optimizerStep != "wordSizeTransform" && !std::get<0>(parse(_stream, _linePrefix, _formatted, printed)))
	{
		util::AnsiColorized(_stream, _formatted, {util::formatting::BOLD, util::formatting::CYAN})
			<< _linePrefix << "Result after the optimiser:" << endl;
		printPrefixed(_stream, printed, _linePrefix + "  ");
		return TestResult::FatalError;
	}

	m_obtainedResult = "step: " + m_optimizerStep + "\n\n" + printed + "\n";

	return checkResult(_stream, _linePrefix, _formatted);
}

std::pair<std::shared_ptr<Object>, std::shared_ptr<AsmAnalysisInfo>> YulOptimizerTest::parse(
	ostream& _stream,
	string const& _linePrefix,
	bool const _formatted,
	string const& _source
)
{
	ErrorList errors;
	hyptestAssert(m_dialect, "");
	shared_ptr<Object> object;
	shared_ptr<AsmAnalysisInfo> analysisInfo;
	std::tie(object, analysisInfo) = yul::test::parse(_source, *m_dialect, errors);
	if (!object || !analysisInfo || Error::containsErrors(errors))
	{
		AnsiColorized(_stream, _formatted, {formatting::BOLD, formatting::RED}) << _linePrefix << "Error parsing source." << endl;
		CharStream charStream(_source, "");
		SourceReferenceFormatter{_stream, SingletonCharStreamProvider(charStream), true, false}
			.printErrorInformation(errors);
		return {};
	}
	return {std::move(object), std::move(analysisInfo)};
}
