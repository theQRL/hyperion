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

#include <libyul/optimiser/StackLimitEvader.h>
#include <libyul/optimiser/CallGraphGenerator.h>
#include <libyul/optimiser/FunctionCallFinder.h>
#include <libyul/optimiser/NameDispenser.h>
#include <libyul/optimiser/NameCollector.h>
#include <libyul/optimiser/StackToMemoryMover.h>
#include <libyul/backends/qrvm/ControlFlowGraphBuilder.h>
#include <libyul/backends/qrvm/QRVMDialect.h>
#include <libyul/AsmAnalysis.h>
#include <libyul/AST.h>
#include <libyul/CompilabilityChecker.h>
#include <libyul/Exceptions.h>
#include <libyul/Object.h>
#include <libyul/Utilities.h>
#include <libhyputil/Algorithms.h>
#include <libhyputil/CommonData.h>
#include <libhyputil/VMConstants.h>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/concat.hpp>
#include <range/v3/view/take.hpp>

using namespace hyperion;
using namespace hyperion::yul;

namespace
{
/**
 * Walks the call graph using a Depth-First-Search assigning memory slots to variables.
 * - The leaves of the call graph will get the lowest slot, increasing towards the root.
 *
 * - Mutually recursive functions cannot be given individual slot counts, because each of them
 *   reaches all the others. They are therefore handled as a unit: the traversal walks the strongly-
 *   connected components of the call graph rather than its individual functions, and every member of
 *   a component is considered to depend on as many slots as the component as a whole.
 *
 * - ``slotsRequiredForComponent`` maps a component to the number of slots it requires (which is also
 *   the next available slot that can be used by another function that calls into the component).
 *
 * - For each component starting from the one containing the root of the call graph:
 *   - Visit all components called by any of its members that are not already visited. Calls within the
 *     component are ignored as they cannot contribute a requirement, see below.
 *   - Determine the maximum value ``n`` of the values of ``slotsRequiredForComponent`` among them.
 *   - Assign each variable of each member its slot starting from ``n`` (incrementing it).
 *   - Assign ``n`` to ``slotsRequiredForComponent`` of the component.
 *
 * Since the components form an acyclic graph, a component is never reached again while it is still
 * being processed, and every value read from ``slotsRequiredForComponent`` is final.
 * Note that members of a component of more than one function never get a slot assigned at all:
 * ``StackLimitEvader::run`` bails out before reaching this point if any recursive function has
 * variables that need to be moved to memory.
 */
struct MemoryOffsetAllocator
{
	uint64_t run(YulString _function = YulString{})
	{
		size_t const componentIndex = callCycles.componentIndexOf(_function);
		if (uint64_t const* slotsRequired = util::valueOrNullptr(slotsRequiredForComponent, componentIndex))
			return *slotsRequired;

		// gather required slots from component-adjacent nodes in the call graph
		uint64_t requiredSlots = 0;
		for (YulString member: callCycles.component(componentIndex))
			if (auto const* children = util::valueOrNullptr(callGraph, member))
				for (YulString child: *children)
					if (callCycles.componentIndexOf(child) != componentIndex)
						// recurse and max if we leave SCC
						requiredSlots = std::max(run(child), requiredSlots);

		for (YulString member: callCycles.component(componentIndex))
			if (auto const* unreachables = util::valueOrNullptr(unreachableVariables, member))
			{
				yulAssert(
					!callCycles.isRecursive(member),
					"Cannot move variables of a recursive function to fixed memory offsets."
				);

				if (FunctionDefinition const* functionDefinition = util::valueOrDefault(functionDefinitions, member, nullptr, util::allow_copy))
					if (
						size_t totalArgCount = functionDefinition->returnVariables.size() + functionDefinition->parameters.size();
						totalArgCount > 16
					)
						for (TypedName const& var: ranges::concat_view(
							functionDefinition->parameters,
							functionDefinition->returnVariables
						) | ranges::views::take(totalArgCount - 16))
							slotAllocations[var.name] = requiredSlots++;

				// Assign slots for all variables that become unreachable in the function body, if the above did not
				// assign a slot for them already.
				for (YulString variable: *unreachables)
					// The empty case is a function with too many arguments or return values,
					// which was already handled above.
					if (!variable.empty() && !slotAllocations.count(variable))
						slotAllocations[variable] = requiredSlots++;
			}

		return slotsRequiredForComponent[componentIndex] = requiredSlots;
	}

	/// Maps function names to the set of unreachable variables in that function.
	/// An empty variable name means that the function has too many arguments or return variables.
	std::map<YulString, std::vector<YulString>> const& unreachableVariables;
	/// The graph of immediate function calls of all functions.
	std::map<YulString, std::vector<YulString>> const& callGraph;
	/// The cycle structure of the call graph
	CallGraphCycles const& callCycles;
	/// Maps the name of each user-defined function to its definition.
	std::map<YulString, FunctionDefinition const*> const& functionDefinitions;

	/// Maps variable names to the memory slot the respective variable is assigned.
	std::map<YulString, uint64_t> slotAllocations{};
	/// Maps strongly connected components to the number of memory slots the respective component requires.
	std::map<size_t, uint64_t> slotsRequiredForComponent{};
};

u512 literalArgumentValue(FunctionCall const& _call)
{
	yulAssert(_call.arguments.size() == 1, "");
	Literal const* literal = std::get_if<Literal>(&_call.arguments.front());
	yulAssert(literal && literal->kind == LiteralKind::Number, "");
	return valueOfLiteral(*literal);
}
}

void StackLimitEvader::run(
	OptimiserStepContext& _context,
	Object& _object
)
{
	auto const* qrvmDialect = dynamic_cast<QRVMDialect const*>(&_context.dialect);
	yulAssert(
		qrvmDialect && qrvmDialect->providesObjectAccess(),
		"StackLimitEvader can only be run on objects using the QRVMDialect with object access."
	);
	if (qrvmDialect)
	{
		yul::AsmAnalysisInfo analysisInfo = yul::AsmAnalyzer::analyzeStrictAssertCorrect(*qrvmDialect, _object);
		std::unique_ptr<CFG> cfg = ControlFlowGraphBuilder::build(analysisInfo, *qrvmDialect, *_object.code);
		run(_context, _object, StackLayoutGenerator::reportStackTooDeep(*cfg));
	}
	else
		run(_context, _object, CompilabilityChecker{
			_context.dialect,
			_object,
			true
		}.unreachableVariables);

}

void StackLimitEvader::run(
	OptimiserStepContext& _context,
	Object& _object,
	std::map<YulString, std::vector<StackLayoutGenerator::StackTooDeep>> const& _stackTooDeepErrors
)
{
	std::map<YulString, std::vector<YulString>> unreachableVariables;
	for (auto&& [function, stackTooDeepErrors]: _stackTooDeepErrors)
	{
		auto& unreachables = unreachableVariables[function];
		// TODO: choose wisely.
		for (auto const& stackTooDeepError: stackTooDeepErrors)
			for (auto variable: stackTooDeepError.variableChoices | ranges::views::take(stackTooDeepError.deficit))
				if (!util::contains(unreachables, variable))
					unreachables.emplace_back(variable);
	}
	run(_context, _object, unreachableVariables);
}

void StackLimitEvader::run(
	OptimiserStepContext& _context,
	Object& _object,
	std::map<YulString, std::vector<YulString>> const& _unreachableVariables
)
{
	yulAssert(_object.code, "");
	auto const* qrvmDialect = dynamic_cast<QRVMDialect const*>(&_context.dialect);
	yulAssert(
		qrvmDialect && qrvmDialect->providesObjectAccess(),
		"StackLimitEvader can only be run on objects using the QRVMDialect with object access."
	);

	std::vector<FunctionCall*> memoryGuardCalls = FunctionCallFinder::run(
		*_object.code,
		"memoryguard"_yulstring
	);
	// Do not optimise, if no ``memoryguard`` call is found.
	if (memoryGuardCalls.empty())
		return;

	// Make sure all calls to ``memoryguard`` we found have the same value as argument (otherwise, abort).
	u512 reservedMemoryLiteral = literalArgumentValue(*memoryGuardCalls.front());
	yulAssert(reservedMemoryLiteral < (u512(1) << 32) - 1, "");
	u256 reservedMemory = u256(reservedMemoryLiteral);

	for (FunctionCall const* memoryGuardCall: memoryGuardCalls)
		if (reservedMemoryLiteral != literalArgumentValue(*memoryGuardCall))
			return;

	CallGraph callGraph = CallGraphGenerator::callGraph(*_object.code);
	CallGraphCycles const callCycles = callGraph.analyzeCallCycles();

	// We cannot move variables in recursive functions to fixed memory offsets.
	for (YulString function: callCycles.recursiveFunctions)
		if (_unreachableVariables.count(function))
			return;

	std::map<YulString, FunctionDefinition const*> functionDefinitions = allFunctionDefinitions(*_object.code);

	// Functions that call each other have to share their slot requirements, so the allocator below
	// traverses the strongly-connected components of the call graph
	MemoryOffsetAllocator memoryOffsetAllocator{_unreachableVariables, callGraph.functionCalls, callCycles, functionDefinitions};
	uint64_t requiredSlots = memoryOffsetAllocator.run();
	yulAssert(requiredSlots < (uint64_t(1) << 32) - 1, "");

	StackToMemoryMover::run(_context, reservedMemory, memoryOffsetAllocator.slotAllocations, requiredSlots, *_object.code);

	reservedMemory += VMWordBytes * requiredSlots;
	for (FunctionCall* memoryGuardCall: FunctionCallFinder::run(*_object.code, "memoryguard"_yulstring))
	{
		Literal* literal = std::get_if<Literal>(&memoryGuardCall->arguments.front());
		yulAssert(literal && literal->kind == LiteralKind::Number, "");
		literal->value = YulString{toCompactHexWithPrefix(reservedMemory)};
	}
}
