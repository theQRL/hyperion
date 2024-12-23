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

#include <test/Common.h>
#include <test/TestCase.h>

#include <libhyputil/AnsiColorized.h>
#include <libhyputil/StringUtils.h>

#include <boost/algorithm/string/predicate.hpp>

#include <iostream>
#include <stdexcept>

using namespace std;
using namespace hyperion;
using namespace hyperion::frontend;
using namespace hyperion::frontend::test;
using namespace hyperion::util;

void TestCase::printSettings(ostream& _stream, const string& _linePrefix, const bool)
{
	auto& settings = m_reader.settings();
	if (settings.empty())
		return;

	_stream << _linePrefix << "// ====" << endl;
	for (auto const& setting: settings)
		_stream << _linePrefix << "// " << setting.first << ": " << setting.second << endl;
}

void TestCase::printUpdatedSettings(std::ostream& _stream, std::string const& _linePrefix)
{
	printSettings(_stream, _linePrefix);
}

bool TestCase::isTestFilename(boost::filesystem::path const& _filename)
{
	string extension = _filename.extension().string();
	return (extension == ".hyp" || extension == ".yul" || extension == ".stack") &&
		   !boost::starts_with(_filename.string(), "~") &&
			!boost::starts_with(_filename.string(), ".");
}

bool TestCase::shouldRun()
{
	m_reader.ensureAllSettingsRead();
	return m_shouldRun;
}

void TestCase::expect(string::iterator& _it, string::iterator _end, string::value_type _c)
{
	if (_it == _end || *_it != _c)
		BOOST_THROW_EXCEPTION(runtime_error(string("Invalid test expectation. Expected: \"") + _c + "\"."));
	++_it;
}

void TestCase::printSource(ostream& _stream, string const& _linePrefix, bool const) const
{
	printPrefixed(_stream, m_source, _linePrefix);
}

void TestCase::printUpdatedExpectations(ostream& _stream, string const& _linePrefix) const
{
	printPrefixed(_stream, m_obtainedResult, _linePrefix);
}

TestCase::TestResult TestCase::checkResult(std::ostream& _stream, const std::string& _linePrefix, bool const _formatted)
{
	if (m_expectation != m_obtainedResult)
	{
		string nextIndentLevel = _linePrefix + "  ";
		util::AnsiColorized(_stream, _formatted, {util::formatting::BOLD, util::formatting::CYAN})
			<< _linePrefix << "Expected result:" << endl;
		// TODO could compute a simple diff with highlighted lines
		printPrefixed(_stream, m_expectation, nextIndentLevel);
		util::AnsiColorized(_stream, _formatted, {util::formatting::BOLD, util::formatting::CYAN})
			<< _linePrefix << "Obtained result:" << endl;
		printPrefixed(_stream, m_obtainedResult, nextIndentLevel);
		return TestResult::Failure;
	}
	return TestResult::Success;
}

ZVMVersionRestrictedTestCase::ZVMVersionRestrictedTestCase(string const& _filename):
	TestCase(_filename)
{
	string versionString = m_reader.stringSetting("ZVMVersion", "any");
	if (versionString == "any")
		return;

	string comparator;
	size_t versionBegin = 0;
	for (auto character: versionString)
		if (!isalpha(character, locale::classic()))
		{
			comparator += character;
			versionBegin++;
		}
		else
			break;

	versionString = versionString.substr(versionBegin);
	std::optional<langutil::ZVMVersion> version = langutil::ZVMVersion::fromString(versionString);
	if (!version)
		BOOST_THROW_EXCEPTION(runtime_error{"Invalid ZVM version: \"" + versionString + "\""});

	langutil::ZVMVersion zvmVersion = hyperion::test::CommonOptions::get().zvmVersion();
	bool comparisonResult;
	if (comparator == ">")
		comparisonResult = zvmVersion > version;
	else if (comparator == ">=")
		comparisonResult = zvmVersion >= version;
	else if (comparator == "<")
		comparisonResult = zvmVersion < version;
	else if (comparator == "<=")
		comparisonResult = zvmVersion <= version;
	else if (comparator == "=")
		comparisonResult = zvmVersion == version;
	else if (comparator == "!")
		comparisonResult = !(zvmVersion == version);
	else
		BOOST_THROW_EXCEPTION(runtime_error{"Invalid ZVM comparator: \"" + comparator + "\""});

	if (!comparisonResult)
		m_shouldRun = false;
}
