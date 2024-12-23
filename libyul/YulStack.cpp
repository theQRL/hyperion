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
 * Full assembly stack that can support ZVM-assembly and Yul as input and ZVM, ZVM1.5
 */


#include <libyul/YulStack.h>

#include <libyul/AsmAnalysis.h>
#include <libyul/AsmAnalysisInfo.h>
#include <libyul/backends/zvm/ZondAssemblyAdapter.h>
#include <libyul/backends/zvm/ZVMCodeTransform.h>
#include <libyul/backends/zvm/ZVMDialect.h>
#include <libyul/backends/zvm/ZVMObjectCompiler.h>
#include <libyul/backends/zvm/ZVMMetrics.h>
#include <libyul/ObjectParser.h>
#include <libyul/optimiser/Semantics.h>
#include <libyul/optimiser/Suite.h>
#include <libzvmasm/Assembly.h>
#include <liblangutil/Scanner.h>
#include <libhyperion/interface/OptimiserSettings.h>

#include <boost/algorithm/string.hpp>

#include <optional>

using namespace hyperion;
using namespace hyperion::frontend;
using namespace hyperion::yul;
using namespace hyperion::langutil;

namespace
{
Dialect const& languageToDialect(YulStack::Language _language, ZVMVersion _version)
{
	switch (_language)
	{
	case YulStack::Language::Assembly:
	case YulStack::Language::StrictAssembly:
		return ZVMDialect::strictAssemblyForZVMObjects(_version);
	case YulStack::Language::Yul:
		return ZVMDialectTyped::instance(_version);
	}
	yulAssert(false, "");
	return Dialect::yulDeprecated();
}

}


CharStream const& YulStack::charStream(std::string const& _sourceName) const
{
	yulAssert(m_charStream, "");
	yulAssert(m_charStream->name() == _sourceName, "");
	return *m_charStream;
}

bool YulStack::parseAndAnalyze(std::string const& _sourceName, std::string const& _source)
{
	m_errors.clear();
	m_analysisSuccessful = false;
	m_charStream = std::make_unique<CharStream>(_source, _sourceName);
	std::shared_ptr<Scanner> scanner = std::make_shared<Scanner>(*m_charStream);
	m_parserResult = ObjectParser(m_errorReporter, languageToDialect(m_language, m_zvmVersion)).parse(scanner, false);
	if (!m_errorReporter.errors().empty())
		return false;
	yulAssert(m_parserResult, "");
	yulAssert(m_parserResult->code, "");

	return analyzeParsed();
}

void YulStack::optimize()
{
	yulAssert(m_analysisSuccessful, "Analysis was not successful.");
	yulAssert(m_parserResult);

	if (
		!m_optimiserSettings.runYulOptimiser &&
		yul::MSizeFinder::containsMSize(languageToDialect(m_language, m_zvmVersion), *m_parserResult)
	)
		return;

	m_analysisSuccessful = false;
	yulAssert(m_parserResult, "");
	optimize(*m_parserResult, true);
	yulAssert(analyzeParsed(), "Invalid source code after optimization.");
}

bool YulStack::analyzeParsed()
{
	yulAssert(m_parserResult, "");
	m_analysisSuccessful = analyzeParsed(*m_parserResult);
	return m_analysisSuccessful;
}

bool YulStack::analyzeParsed(Object& _object)
{
	yulAssert(_object.code, "");
	_object.analysisInfo = std::make_shared<AsmAnalysisInfo>();

	AsmAnalyzer analyzer(
		*_object.analysisInfo,
		m_errorReporter,
		languageToDialect(m_language, m_zvmVersion),
		{},
		_object.qualifiedDataNames()
	);
	bool success = analyzer.analyze(*_object.code);
	for (auto& subNode: _object.subObjects)
		if (auto subObject = dynamic_cast<Object*>(subNode.get()))
			if (!analyzeParsed(*subObject))
				success = false;
	return success;
}

void YulStack::compileZVM(AbstractAssembly& _assembly, bool _optimize) const
{
	ZVMDialect const* dialect = nullptr;
	switch (m_language)
	{
		case Language::Assembly:
		case Language::StrictAssembly:
			dialect = &ZVMDialect::strictAssemblyForZVMObjects(m_zvmVersion);
			break;
		case Language::Yul:
			dialect = &ZVMDialectTyped::instance(m_zvmVersion);
			break;
		default:
			yulAssert(false, "Invalid language.");
			break;
	}

	ZVMObjectCompiler::compile(*m_parserResult, _assembly, *dialect, _optimize);
}

void YulStack::optimize(Object& _object, bool _isCreation)
{
	yulAssert(_object.code, "");
	yulAssert(_object.analysisInfo, "");
	for (auto& subNode: _object.subObjects)
		if (auto subObject = dynamic_cast<Object*>(subNode.get()))
		{
			bool isCreation = !boost::ends_with(subObject->name.str(), "_deployed");
			optimize(*subObject, isCreation);
		}

	Dialect const& dialect = languageToDialect(m_language, m_zvmVersion);
	std::unique_ptr<GasMeter> meter;
	if (ZVMDialect const* zvmDialect = dynamic_cast<ZVMDialect const*>(&dialect))
		meter = std::make_unique<GasMeter>(*zvmDialect, _isCreation, m_optimiserSettings.expectedExecutionsPerDeployment);

	auto [optimizeStackAllocation, yulOptimiserSteps, yulOptimiserCleanupSteps] = [&]() -> std::tuple<bool, std::string, std::string>
	{
		if (!m_optimiserSettings.runYulOptimiser)
		{
			// Yul optimizer disabled, but empty sequence (:) explicitly provided
			if (OptimiserSuite::isEmptyOptimizerSequence(m_optimiserSettings.yulOptimiserSteps + ":" + m_optimiserSettings.yulOptimiserCleanupSteps))
				return std::make_tuple(true, "", "");
			// Yul optimizer disabled, and no sequence explicitly provided (assumes default sequence)
			else
			{
				yulAssert(
					m_optimiserSettings.yulOptimiserSteps == OptimiserSettings::DefaultYulOptimiserSteps &&
					m_optimiserSettings.yulOptimiserCleanupSteps == OptimiserSettings::DefaultYulOptimiserCleanupSteps
				);
				return std::make_tuple(true, "u", "");
			}

		}
		return std::make_tuple(
			m_optimiserSettings.optimizeStackAllocation,
			m_optimiserSettings.yulOptimiserSteps,
			m_optimiserSettings.yulOptimiserCleanupSteps
		);
	}();

	OptimiserSuite::run(
		dialect,
		meter.get(),
		_object,
		// Defaults are the minimum necessary to avoid running into "Stack too deep" constantly.
		optimizeStackAllocation,
		yulOptimiserSteps,
		yulOptimiserCleanupSteps,
		_isCreation ? std::nullopt : std::make_optional(m_optimiserSettings.expectedExecutionsPerDeployment),
		{}
	);
}

MachineAssemblyObject YulStack::assemble(Machine _machine) const
{
	yulAssert(m_analysisSuccessful, "");
	yulAssert(m_parserResult, "");
	yulAssert(m_parserResult->code, "");
	yulAssert(m_parserResult->analysisInfo, "");

	switch (_machine)
	{
	case Machine::ZVM:
		return assembleWithDeployed().first;
	}
	// unreachable
	return MachineAssemblyObject();
}

std::pair<MachineAssemblyObject, MachineAssemblyObject>
YulStack::assembleWithDeployed(std::optional<std::string_view> _deployName) const
{
	auto [creationAssembly, deployedAssembly] = assembleZVMWithDeployed(_deployName);
	yulAssert(creationAssembly, "");
	yulAssert(m_charStream, "");

	MachineAssemblyObject creationObject;
	creationObject.bytecode = std::make_shared<zvmasm::LinkerObject>(creationAssembly->assemble());
	yulAssert(creationObject.bytecode->immutableReferences.empty(), "Leftover immutables.");
	creationObject.assembly = creationAssembly->assemblyString(m_debugInfoSelection);
	creationObject.sourceMappings = std::make_unique<std::string>(
		zvmasm::AssemblyItem::computeSourceMapping(
			creationAssembly->items(),
			{{m_charStream->name(), 0}}
		)
	);

	MachineAssemblyObject deployedObject;
	if (deployedAssembly)
	{
		deployedObject.bytecode = std::make_shared<zvmasm::LinkerObject>(deployedAssembly->assemble());
		deployedObject.assembly = deployedAssembly->assemblyString(m_debugInfoSelection);
		deployedObject.sourceMappings = std::make_unique<std::string>(
			zvmasm::AssemblyItem::computeSourceMapping(
				deployedAssembly->items(),
				{{m_charStream->name(), 0}}
			)
		);
	}

	return {std::move(creationObject), std::move(deployedObject)};
}

std::pair<std::shared_ptr<zvmasm::Assembly>, std::shared_ptr<zvmasm::Assembly>>
YulStack::assembleZVMWithDeployed(std::optional<std::string_view> _deployName) const
{
	yulAssert(m_analysisSuccessful, "");
	yulAssert(m_parserResult, "");
	yulAssert(m_parserResult->code, "");
	yulAssert(m_parserResult->analysisInfo, "");

	zvmasm::Assembly assembly(m_zvmVersion, true, {});
	ZondAssemblyAdapter adapter(assembly);

	// NOTE: We always need stack optimization when Yul optimizer is disabled (unless code contains
	// msize). It being disabled just means that we don't use the full step sequence. We still run
	// it with the minimal steps required to avoid "stack too deep".
	bool optimize = m_optimiserSettings.optimizeStackAllocation || (
		!m_optimiserSettings.runYulOptimiser &&
		!yul::MSizeFinder::containsMSize(languageToDialect(m_language, m_zvmVersion), *m_parserResult)
	);
	compileZVM(adapter, optimize);

	assembly.optimise(zvmasm::Assembly::OptimiserSettings::translateSettings(m_optimiserSettings, m_zvmVersion));

	std::optional<size_t> subIndex;

	// Pick matching assembly if name was given
	if (_deployName.has_value())
	{
		for (size_t i = 0; i < assembly.numSubs(); i++)
			if (assembly.sub(i).name() == _deployName)
			{
				subIndex = i;
				break;
			}

		hypAssert(subIndex.has_value(), "Failed to find object to be deployed.");
	}
	// Otherwise use heuristic: If there is a single sub-assembly, this is likely the object to be deployed.
	else if (assembly.numSubs() == 1)
		subIndex = 0;

	if (subIndex.has_value())
	{
		zvmasm::Assembly& runtimeAssembly = assembly.sub(*subIndex);
		return {std::make_shared<zvmasm::Assembly>(assembly), std::make_shared<zvmasm::Assembly>(runtimeAssembly)};
	}

	return {std::make_shared<zvmasm::Assembly>(assembly), {}};
}

std::string YulStack::print(
	CharStreamProvider const* _hyperionSourceProvider
) const
{
	yulAssert(m_parserResult, "");
	yulAssert(m_parserResult->code, "");
	return m_parserResult->toString(&languageToDialect(m_language, m_zvmVersion), m_debugInfoSelection, _hyperionSourceProvider) + "\n";
}

Json::Value YulStack::astJson() const
{
	yulAssert(m_parserResult, "");
	yulAssert(m_parserResult->code, "");
	return  m_parserResult->toJson();
}

std::shared_ptr<Object> YulStack::parserResult() const
{
	yulAssert(m_analysisSuccessful, "Analysis was not successful.");
	yulAssert(m_parserResult, "");
	yulAssert(m_parserResult->code, "");
	return m_parserResult;
}
