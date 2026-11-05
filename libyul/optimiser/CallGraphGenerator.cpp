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
 * Specific AST walker that generates the call graph.
 */

#include <libyul/optimiser/CallGraphGenerator.h>

#include <libyul/AST.h>
#include <libyul/Exceptions.h>

#include <libhyputil/CommonData.h>
#include <libhyputil/TarjanSCC.h>

#include <range/v3/algorithm/binary_search.hpp>
#include <range/v3/algorithm/sort.hpp>
#include <range/v3/algorithm/unique.hpp>

#include <cstddef>

using namespace hyperion;
using namespace hyperion::yul;
using namespace hyperion::util;

namespace
{

class FunctionToIndexBiMapping
{
public:
	explicit FunctionToIndexBiMapping(std::map<YulString, std::vector<YulString>> const& _functionCalls)
	{
		for (auto const& [function, callees]: _functionCalls)
		{
			if (m_functionToIndex.try_emplace(function, m_indexToFunction.size()).second)
				m_indexToFunction.emplace_back(function);
			for (auto const& callee: callees)
				if (m_functionToIndex.try_emplace(callee, m_indexToFunction.size()).second)
					m_indexToFunction.emplace_back(callee);
		}
	}

	YulString indexToFunction(std::size_t const _index) const
	{
		return m_indexToFunction.at(_index);
	}

	std::size_t numFunctions() const
	{
		return m_indexToFunction.size();
	}

	std::size_t functionToIndex(YulString const _function) const
	{
		return m_functionToIndex.at(_function);
	}

private:
	std::vector<YulString> m_indexToFunction;
	std::map<YulString, std::size_t> m_functionToIndex;
};

}

CallGraphCycles CallGraph::analyzeCallCycles() const
{
	// A function is recursive iff it is part of a non-trivial strongly-connected component of the call graph (a
	// mutual-recursion cycle of any length) or it directly calls itself. The SCCs are computed with Tarjan's algorithm.
	// Tarjan's implementation requires dense node indices in [0, N), so assign each function
	// (both callers and callees) a consecutive index.
	FunctionToIndexBiMapping const functionIndexBimap(functionCalls);

	// Build list of edges in the call graph
	std::vector<std::vector<std::size_t>> indexBasedAdjacencyList(functionIndexBimap.numFunctions());
	for (auto const& [function, callees]: functionCalls)
		for (auto const& callee: callees)
			indexBasedAdjacencyList[functionIndexBimap.functionToIndex(function)].emplace_back(functionIndexBimap.functionToIndex(callee));

	// Sort and deduplicate each adjacency list so the self-loop check below can use a binary search
	for (auto& callees: indexBasedAdjacencyList)
	{
		ranges::sort(callees);
		callees.erase(ranges::unique(callees), callees.end());
	}

	std::vector<std::vector<YulString>> components;
	std::map<YulString, std::size_t> componentOfFunction;
	std::set<YulString> recursiveFunctionSet;
	for (std::vector<std::size_t> const& scc: util::computeStronglyConnectedComponents<std::size_t>(indexBasedAdjacencyList))
	{
		yulAssert(!scc.empty());
		std::size_t const componentIndex = components.size();
		std::vector<YulString>& component = components.emplace_back();
		for (std::size_t const node: scc)
		{
			component.emplace_back(functionIndexBimap.indexToFunction(node));
			componentOfFunction[component.back()] = componentIndex;
		}
		if (component.size() > 1)
			// more than one element in the SCC: everything in it is mutually recursive
			recursiveFunctionSet.insert(component.begin(), component.end());
		else if (ranges::binary_search(indexBasedAdjacencyList[scc.front()], scc.front()))
			// self-recursion f -> f
			recursiveFunctionSet.insert(component.front());
	}

	yulAssert(!recursiveFunctionSet.count(YulString{}), "the top-level block cannot be recursive");
	// Builtins never get an entry in ``functionCalls``, since only function definitions and the
	// top-level block do, so they have no outgoing edges and cannot be part of a cycle.
	for (YulString const recursiveFunction: recursiveFunctionSet)
		yulAssert(functionCalls.count(recursiveFunction), "a builtin cannot be recursive");

	CallGraphCycles result;
	result.stronglyConnectedComponents = std::move(components);
	result.componentOfFunction = std::move(componentOfFunction);
	result.recursiveFunctions = std::move(recursiveFunctionSet);
	return result;
}

std::set<YulString> CallGraph::recursiveFunctions() const
{
	return analyzeCallCycles().recursiveFunctions;
}

CallGraph CallGraphGenerator::callGraph(Block const& _ast)
{
	CallGraphGenerator gen;
	gen(_ast);
	return std::move(gen.m_callGraph);
}

void CallGraphGenerator::operator()(FunctionCall const& _functionCall)
{
	auto& functionCalls = m_callGraph.functionCalls[m_currentFunction];
	if (!util::contains(functionCalls, _functionCall.functionName.name))
		functionCalls.emplace_back(_functionCall.functionName.name);
	ASTWalker::operator()(_functionCall);
}

void CallGraphGenerator::operator()(ForLoop const& _forLoop)
{
	m_callGraph.functionsWithLoops.insert(m_currentFunction);
	ASTWalker::operator()(_forLoop);
}

void CallGraphGenerator::operator()(FunctionDefinition const& _functionDefinition)
{
	solRequire(
		!m_callGraph.functionCalls.count(_functionDefinition.name),
		InputNotDisambiguatedException,
		"CallGraphGenerator requires a disambiguated AST: duplicate function name " + _functionDefinition.name.str() + "."
	);
	YulString previousFunction = m_currentFunction;
	m_currentFunction = _functionDefinition.name;
	m_callGraph.functionCalls[m_currentFunction] = {};
	ASTWalker::operator()(_functionDefinition);
	m_currentFunction = previousFunction;
}

CallGraphGenerator::CallGraphGenerator()
{
	m_callGraph.functionCalls[YulString{}] = {};
}

