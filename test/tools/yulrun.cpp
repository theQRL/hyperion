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
/**
 * Yul interpreter.
 */

#include <test/tools/yulInterpreter/Interpreter.h>
#include <test/tools/yulInterpreter/Inspector.h>

#include <libyul/AsmAnalysisInfo.h>
#include <libyul/AsmAnalysis.h>
#include <libyul/Dialect.h>
#include <libyul/backends/zvm/ZVMDialect.h>
#include <libyul/YulStack.h>

#include <liblangutil/DebugInfoSelection.h>
#include <liblangutil/Exceptions.h>
#include <liblangutil/ZVMVersion.h>
#include <liblangutil/SourceReferenceFormatter.h>

#include <libhyputil/CommonIO.h>
#include <libhyputil/CommonData.h>
#include <libhyputil/Exceptions.h>

#include <boost/program_options.hpp>

#include <string>
#include <memory>
#include <iostream>

using namespace std;
using namespace hyperion;
using namespace hyperion::util;
using namespace hyperion::langutil;
using namespace hyperion::yul;
using namespace hyperion::yul::test;

namespace po = boost::program_options;

namespace
{

pair<shared_ptr<Block>, shared_ptr<AsmAnalysisInfo>> parse(string const& _source)
{
	YulStack stack(
		langutil::ZVMVersion(),
		YulStack::Language::StrictAssembly,
		hyperion::frontend::OptimiserSettings::none(),
		DebugInfoSelection::Default()
	);
	if (stack.parseAndAnalyze("--INPUT--", _source))
	{
		yulAssert(stack.errors().empty(), "Parsed successfully but had errors.");
		return make_pair(stack.parserResult()->code, stack.parserResult()->analysisInfo);
	}
	else
	{
		SourceReferenceFormatter(cout, stack, true, false).printErrorInformation(stack.errors());
		return {};
	}
}

void interpret(string const& _source, bool _inspect, bool _disableExternalCalls)
{
	shared_ptr<Block> ast;
	shared_ptr<AsmAnalysisInfo> analysisInfo;
	tie(ast, analysisInfo) = parse(_source);
	if (!ast || !analysisInfo)
		return;

	InterpreterState state;
	state.maxTraceSize = 10000;
	try
	{
		Dialect const& dialect(ZVMDialect::strictAssemblyForZVMObjects(langutil::ZVMVersion{}));

		if (_inspect)
			InspectedInterpreter::run(std::make_shared<Inspector>(_source, state), state, dialect, *ast, _disableExternalCalls, /*disableMemoryTracing=*/false);

		else
			Interpreter::run(state, dialect, *ast, _disableExternalCalls, /*disableMemoryTracing=*/false);
	}
	catch (InterpreterTerminatedGeneric const&)
	{
	}

	state.dumpTraceAndState(cout, /*disableMemoryTracing=*/false);
}

}

int main(int argc, char** argv)
{
	po::options_description options(
		R"(yulrun, the Yul interpreter.
Usage: yulrun [Options] < input
Reads a single source from stdin, runs it and prints a trace of all side-effects.

Allowed options)",
		po::options_description::m_default_line_length,
		po::options_description::m_default_line_length - 23);
	options.add_options()
		("help", "Show this help screen.")
		("enable-external-calls", "Enable external calls")
		("interactive", "Run interactive")
		("input-file", po::value<vector<string>>(), "input file");
	po::positional_options_description filesPositions;
	filesPositions.add("input-file", -1);

	po::variables_map arguments;
	try
	{
		po::command_line_parser cmdLineParser(argc, argv);
		cmdLineParser.options(options).positional(filesPositions);
		po::store(cmdLineParser.run(), arguments);
	}
	catch (po::error const& _exception)
	{
		cerr << _exception.what() << endl;
		return 1;
	}

	if (arguments.count("help"))
		cout << options;
	else
	{
		string input;
		if (arguments.count("input-file"))
			for (string path: arguments["input-file"].as<vector<string>>())
			{
				try
				{
					input += readFileAsString(path);
				}
				catch (FileNotFound const&)
				{
					cerr << "File not found: " << path << endl;
					return 1;
				}
				catch (NotAFile const&)
				{
					cerr << "Not a regular file: " << path << endl;
					return 1;
				}
			}
		else
			input = readUntilEnd(cin);

		interpret(input, arguments.count("interactive"), !arguments.count("enable-external-calls"));
	}

	return 0;
}
