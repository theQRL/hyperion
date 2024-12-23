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
/**
 * @date 2017
 * Common functions the Yul tests.
 */

#include <test/libyul/Common.h>

#include <test/Common.h>

#include <libyul/optimiser/Disambiguator.h>
#include <libyul/AsmAnalysis.h>
#include <libyul/AsmPrinter.h>
#include <libyul/YulStack.h>
#include <libyul/AST.h>
#include <libyul/backends/zvm/ZVMDialect.h>

#include <liblangutil/DebugInfoSelection.h>
#include <liblangutil/ErrorReporter.h>
#include <liblangutil/Scanner.h>

#include <boost/test/unit_test.hpp>

#include <variant>

using namespace std;
using namespace hyperion;
using namespace hyperion::yul;
using namespace hyperion::langutil;

namespace
{
Dialect const& defaultDialect(bool _yul)
{
	return _yul ? yul::Dialect::yulDeprecated() : yul::ZVMDialect::strictAssemblyForZVM(hyperion::test::CommonOptions::get().zvmVersion());
}
}

pair<shared_ptr<Block>, shared_ptr<yul::AsmAnalysisInfo>> yul::test::parse(string const& _source, bool _yul)
{
	YulStack stack(
		hyperion::test::CommonOptions::get().zvmVersion(),
		_yul ? YulStack::Language::Yul : YulStack::Language::StrictAssembly,
		hyperion::test::CommonOptions::get().optimize ?
			hyperion::frontend::OptimiserSettings::standard() :
			hyperion::frontend::OptimiserSettings::minimal(),
		DebugInfoSelection::All()
	);
	if (!stack.parseAndAnalyze("", _source) || !stack.errors().empty())
		BOOST_FAIL("Invalid source.");
	return make_pair(stack.parserResult()->code, stack.parserResult()->analysisInfo);
}

pair<shared_ptr<Object>, shared_ptr<yul::AsmAnalysisInfo>> yul::test::parse(
	string const& _source,
	Dialect const& _dialect,
	ErrorList& _errors
)
{
	ErrorReporter errorReporter(_errors);
	CharStream stream(_source, "");
	shared_ptr<Scanner> scanner = make_shared<Scanner>(stream);
	shared_ptr<Object> parserResult = yul::ObjectParser(errorReporter, _dialect).parse(scanner, false);
	if (!parserResult)
		return {};
	if (!parserResult->code || errorReporter.hasErrors())
		return {};
	shared_ptr<AsmAnalysisInfo> analysisInfo = make_shared<AsmAnalysisInfo>();
	AsmAnalyzer analyzer(*analysisInfo, errorReporter, _dialect, {}, parserResult->qualifiedDataNames());
	// TODO this should be done recursively.
	if (!analyzer.analyze(*parserResult->code) || errorReporter.hasErrors())
		return {};
	return {std::move(parserResult), std::move(analysisInfo)};
}

yul::Block yul::test::disambiguate(string const& _source, bool _yul)
{
	auto result = parse(_source, _yul);
	return std::get<Block>(Disambiguator(defaultDialect(_yul), *result.second, {})(*result.first));
}

string yul::test::format(string const& _source, bool _yul)
{
	return yul::AsmPrinter()(*parse(_source, _yul).first);
}

namespace
{
std::map<string const, yul::Dialect const& (*)(langutil::ZVMVersion)> const validDialects = {
	{
		"zvm",
		[](langutil::ZVMVersion _zvmVersion) -> yul::Dialect const&
		{ return yul::ZVMDialect::strictAssemblyForZVMObjects(_zvmVersion); }
	},
	{
		"zvmTyped",
		[](langutil::ZVMVersion _zvmVersion) -> yul::Dialect const&
		{ return yul::ZVMDialectTyped::instance(_zvmVersion); }
	},
	{
		"yul",
		[](langutil::ZVMVersion) -> yul::Dialect const&
		{ return yul::Dialect::yulDeprecated(); }
	}
};

vector<string> validDialectNames()
{
	vector<string> names{size(validDialects), ""};
	transform(begin(validDialects), end(validDialects), names.begin(), [](auto const& dialect) { return dialect.first; });

	return names;
}
}

yul::Dialect const& yul::test::dialect(std::string const& _name, langutil::ZVMVersion _zvmVersion)
{
	if (!validDialects.count(_name))
		BOOST_THROW_EXCEPTION(runtime_error{
			"Invalid Dialect \"" +
			_name +
			"\". Valid dialects are " +
			util::joinHumanReadable(validDialectNames(), ", ", " and ") +
			"."
		});

	return validDialects.at(_name)(_zvmVersion);
}
