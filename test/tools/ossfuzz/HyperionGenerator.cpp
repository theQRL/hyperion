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

#include <test/tools/ossfuzz/HyperionGenerator.h>

#include <libhyputil/Whiskers.h>
#include <libhyputil/Visitor.h>

using namespace hyperion::test::fuzzer::mutator;
using namespace hyperion::util;
using namespace std;

GeneratorBase::GeneratorBase(std::shared_ptr<HyperionGenerator> _mutator)
{
	mutator = std::move(_mutator);
	state = mutator->testState();
	uRandDist = mutator->uniformRandomDist();
}

string GeneratorBase::visitChildren()
{
	ostringstream os;
	// Randomise visit order
	vector<GeneratorPtr> randomisedChildren;
	for (auto const& child: generators)
		randomisedChildren.push_back(child);
	shuffle(randomisedChildren.begin(), randomisedChildren.end(), *uRandDist->randomEngine);
	for (auto child: randomisedChildren)
		os << std::visit(GenericVisitor{
			[&](auto const& _item) { return _item->generate(); }
		}, child);
	return os.str();
}

string TestState::randomPath(set<string> const& _sourceUnitPaths) const
{
	auto it = _sourceUnitPaths.begin();
	/// Advance iterator by n where 0 <= n <= sourceUnitPaths.size() - 1
	size_t increment = uRandDist->distributionOneToN(_sourceUnitPaths.size()) - 1;
	hypAssert(
		increment >= 0 && increment < _sourceUnitPaths.size(),
		"Hypc custom mutator: Invalid increment"
	);
	advance(it, increment);
	return *it;
}

string TestState::randomPath() const
{
	hypAssert(!empty(), "Hypc custom mutator: Null test state");
	return randomPath(sourceUnitPaths);
}

void TestState::print(std::ostream& _os) const
{
	_os << "Printing test state" << std::endl;
	for (auto const& item: sourceUnitPaths)
		_os << "Source path: " << item << std::endl;
}

string TestState::randomNonCurrentPath() const
{
	/// To obtain a source path that is not the currently visited
	/// source unit itself, we require at least one other source
	/// unit to be previously visited.
	hypAssert(size() >= 2, "Hypc custom mutator: Invalid test state");

	set<string> filteredSourcePaths;
	string currentPath = currentSourceUnitPath;
	copy_if(
		sourceUnitPaths.begin(),
		sourceUnitPaths.end(),
		inserter(filteredSourcePaths, filteredSourcePaths.begin()),
		[currentPath](string const& _item) {
			return _item != currentPath;
		}
	);
	return randomPath(filteredSourcePaths);
}

void TestCaseGenerator::setup()
{
	addGenerators({
		mutator->generator<SourceUnitGenerator>()
	});
}

string TestCaseGenerator::visit()
{
	ostringstream os;
	for (unsigned i = 0; i < uRandDist->distributionOneToN(s_maxSourceUnits); i++)
	{
		string sourcePath = path();
		os << "\n"
			<< "==== Source: "
			<< sourcePath
			<< " ===="
	        << "\n";
		updateSourcePath(sourcePath);
		m_numSourceUnits++;
		os << visitChildren();
	}
	return os.str();
}

void SourceUnitGenerator::setup()
{
	addGenerators({
		mutator->generator<ImportGenerator>(),
		mutator->generator<PragmaGenerator>()
	});
}

string SourceUnitGenerator::visit()
{
	return visitChildren();
}

string PragmaGenerator::visit()
{
	static constexpr const char* preamble = R"(
		pragma hyperion >= 0.0.0;
		pragma experimental SMTChecker;
	)";
	// Choose equally at random from coder v1 and v2
	string abiPragma = "pragma abicoder v" +
		to_string(uRandDist->distributionOneToN(2)) +
		";\n";
	return preamble + abiPragma;
}

string ImportGenerator::visit()
{
	/*
	 * Case 1: No source units defined
	 * Case 2: One source unit defined
	 * Case 3: At least two source units defined
	 */
	ostringstream os;
	// Self import with a small probability only if
	// there is one source unit present in test.
	if (state->size() == 1)
	{
		if (uRandDist->probable(s_selfImportInvProb))
			os << "import "
			   << "\""
			   << state->randomPath()
			   << "\";";
	}
	else
	{
		// Import a different source unit if at least
		// two source units available.
		os << "import "
			<< "\""
			<< state->randomNonCurrentPath()
			<< "\";";
	}
	return os.str();
}

template <typename T>
shared_ptr<T> HyperionGenerator::generator()
{
	for (auto& g: m_generators)
		if (holds_alternative<shared_ptr<T>>(g))
			return get<shared_ptr<T>>(g);
	hypAssert(false, "");
}

HyperionGenerator::HyperionGenerator(unsigned _seed)
{
	m_generators = {};
	m_urd = make_shared<UniformRandomDistribution>(make_unique<RandomEngine>(_seed));
	m_state = make_shared<TestState>(m_urd);
}

template <size_t I>
void HyperionGenerator::createGenerators()
{
	if constexpr (I < std::variant_size_v<Generator>)
	{
		createGenerator<std::variant_alternative_t<I, Generator>>();
		createGenerators<I + 1>();
	}
}

string HyperionGenerator::generateTestProgram()
{
	createGenerators();
	for (auto& g: m_generators)
		std::visit(GenericVisitor{
			[&](auto const& _item) { return _item->setup(); }
		}, g);
	string program = generator<TestCaseGenerator>()->generate();
	destroyGenerators();
	return program;
}
