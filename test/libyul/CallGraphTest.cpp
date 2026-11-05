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

#include <test/libyul/CallGraphTest.h>

#include <test/libyul/Common.h>
#include <test/Common.h>

#include <libyul/optimiser/ASTWalker.h>
#include <libyul/optimiser/CallGraphGenerator.h>

#include <libyul/AST.h>
#include <libyul/Exceptions.h>
#include <libyul/Object.h>

#include <liblangutil/Exceptions.h>

#include <libhyputil/AnsiColorized.h>
#include <libhyputil/StringUtils.h>

#include <sstream>

using namespace hyperion;
using namespace hyperion::util;
using namespace hyperion::langutil;
using namespace hyperion::yul;
using namespace hyperion::yul::test;
using namespace hyperion::frontend;
using namespace hyperion::frontend::test;

namespace
{

/// Collects function names in the order in which the functions are defined, descending into nested
/// functions, so that the call graph can be printed deterministically.
struct FunctionNameCollector: ASTWalker
{
	using ASTWalker::operator();
	void operator()(FunctionDefinition const& _function) override
	{
		names.emplace_back(_function.name);
		ASTWalker::operator()(_function);
	}

	std::vector<YulString> names;
};

}

std::unique_ptr<TestCase> CallGraphTest::create(Config const& _config)
{
	return std::make_unique<CallGraphTest>(_config.filename);
}

CallGraphTest::CallGraphTest(std::string const& _filename):
	TestCase(_filename)
{
	m_source = m_reader.source();
	m_dialect = &dialect("qrvm", hyperion::test::CommonOptions::get().qrvmVersion());
	m_expectation = m_reader.simpleExpectations();
}

TestCase::TestResult CallGraphTest::run(std::ostream& _stream, std::string const& _linePrefix, bool const _formatted)
{
	ErrorList errors;
	auto [object, analysisInfo] = parse(m_source, *m_dialect, errors);
	if (!object || !analysisInfo || Error::containsErrors(errors))
	{
		AnsiColorized(_stream, _formatted, {formatting::BOLD, formatting::RED}) << _linePrefix << "Error parsing source." << std::endl;
		return TestResult::FatalError;
	}
	hypUnimplementedAssert(object->subObjects.empty(), "Tests with subobjects not supported.");

	Block const& root = *object->code;

	std::ostringstream out;
	try
	{
		CallGraph const callGraph = CallGraphGenerator::callGraph(root);
		std::set<YulString> const recursiveFunctions = callGraph.recursiveFunctions();

		auto printNode = [&](YulString const _name) {
			out << (_name.empty() ? "<main>" : _name.str());

			std::vector<std::string> annotations;
			if (recursiveFunctions.count(_name))
				annotations.emplace_back("recursive");
			if (callGraph.functionsWithLoops.count(_name))
				annotations.emplace_back("loops");
			if (!annotations.empty())
				out << " (" << joinHumanReadable(annotations) << ")";

			std::vector<std::string> renderedCallees;
			for (YulString const& callee: callGraph.functionCalls.at(_name))
				renderedCallees.emplace_back(callee.str());
			if (!renderedCallees.empty())
				out << " -> " << joinHumanReadable(renderedCallees);

			out << "\n";
		};

		FunctionNameCollector collector;
		collector(root);

		// The outermost (non-function) context is denoted by the empty name in the call graph.
		printNode(YulString{});
		for (YulString const& name: collector.names)
			printNode(name);

		yulAssert(!recursiveFunctions.count(YulString{}), "main cannot be recursive");
	}
	catch (CallGraphGenerator::InputNotDisambiguatedException const& _exception)
	{
		out << "InputNotDisambiguatedException: " << (_exception.comment() ? *_exception.comment() : "") << "\n";
	}
	m_obtainedResult = out.str();

	return checkResult(_stream, _linePrefix, _formatted);
}
