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
 * @author Christian <c@ethdev.com>
 * @author Gav Wood <g@ethdev.com>
 * @date 2014
 * Full-stack compiler that converts a source code string to bytecode.
 */


#include <libhyperion/interface/CompilerStack.h>
#include <libhyperion/interface/ImportRemapper.h>

#include <libhyperion/analysis/ControlFlowAnalyzer.h>
#include <libhyperion/analysis/ControlFlowGraph.h>
#include <libhyperion/analysis/ControlFlowRevertPruner.h>
#include <libhyperion/analysis/ContractLevelChecker.h>
#include <libhyperion/analysis/DeclarationTypeChecker.h>
#include <libhyperion/analysis/DocStringAnalyser.h>
#include <libhyperion/analysis/DocStringTagParser.h>
#include <libhyperion/analysis/GlobalContext.h>
#include <libhyperion/analysis/NameAndTypeResolver.h>
#include <libhyperion/analysis/PostTypeChecker.h>
#include <libhyperion/analysis/PostTypeContractLevelChecker.h>
#include <libhyperion/analysis/StaticAnalyzer.h>
#include <libhyperion/analysis/SyntaxChecker.h>
#include <libhyperion/analysis/Scoper.h>
#include <libhyperion/analysis/TypeChecker.h>
#include <libhyperion/analysis/ViewPureChecker.h>
#include <libhyperion/analysis/ImmutableValidator.h>

#include <libhyperion/ast/AST.h>
#include <libhyperion/ast/TypeProvider.h>
#include <libhyperion/ast/ASTJsonImporter.h>
#include <libhyperion/codegen/Compiler.h>
#include <libhyperion/formal/ModelChecker.h>
#include <libhyperion/interface/ABI.h>
#include <libhyperion/interface/Natspec.h>
#include <libhyperion/interface/GasEstimator.h>
#include <libhyperion/interface/StorageLayout.h>
#include <libhyperion/interface/Version.h>
#include <libhyperion/parsing/Parser.h>

#include <libhyperion/experimental/analysis/Analysis.h>

#include <libhyperion/codegen/ir/Common.h>
#include <libhyperion/codegen/ir/IRGenerator.h>

#include <libstdlib/stdlib.h>

#include <libyul/YulString.h>
#include <libyul/AsmPrinter.h>
#include <libyul/AsmJsonConverter.h>
#include <libyul/YulStack.h>
#include <libyul/AST.h>
#include <libyul/AsmParser.h>
#include <libyul/optimiser/Suite.h>

#include <liblangutil/Scanner.h>
#include <liblangutil/SemVerHandler.h>
#include <liblangutil/SourceReferenceFormatter.h>


#include <libhyputil/SwarmHash.h>
#include <libhyputil/IpfsHash.h>
#include <libhyputil/JSON.h>
#include <libhyputil/Algorithms.h>
#include <libhyputil/FunctionSelector.h>

#include <json/json.h>

#include <boost/algorithm/string/replace.hpp>

#include <range/v3/view/concat.hpp>
#include <range/v3/view/map.hpp>

#include <fmt/format.h>

#include <utility>
#include <map>
#include <limits>
#include <string>

using namespace hyperion;
using namespace hyperion::langutil;
using namespace hyperion::frontend;
using namespace hyperion::stdlib;
using namespace hyperion::yul;
using namespace std::string_literals;

using hyperion::util::errinfo_comment;

static int g_compilerStackCounts = 0;

CompilerStack::CompilerStack(ReadCallback::Callback _readFile):
	m_readFile{std::move(_readFile)},
	m_errorReporter{m_errorList}
{
	// Because TypeProvider is currently a singleton API, we must ensure that
	// no more than one entity is actually using it at a time.
	hypAssert(g_compilerStackCounts == 0, "You shall not have another CompilerStack aside me.");
	++g_compilerStackCounts;
}

CompilerStack::~CompilerStack()
{
	--g_compilerStackCounts;
	TypeProvider::reset();
}

void CompilerStack::createAndAssignCallGraphs()
{
	for (Source const* source: m_sourceOrder)
	{
		if (!source->ast)
			continue;

		for (ContractDefinition const* contract: ASTNode::filteredNodes<ContractDefinition>(source->ast->nodes()))
		{
			ContractDefinitionAnnotation& annotation =
				m_contracts.at(contract->fullyQualifiedName()).contract->annotation();

			annotation.creationCallGraph = std::make_unique<CallGraph>(
				FunctionCallGraphBuilder::buildCreationGraph(*contract)
			);
			annotation.deployedCallGraph = std::make_unique<CallGraph>(
				FunctionCallGraphBuilder::buildDeployedGraph(
					*contract,
					**annotation.creationCallGraph
				)
			);

			hypAssert(annotation.contractDependencies.empty(), "contractDependencies expected to be empty?!");

			annotation.contractDependencies = annotation.creationCallGraph->get()->bytecodeDependency;

			for (auto const& [dependencyContract, referencee]: annotation.deployedCallGraph->get()->bytecodeDependency)
				annotation.contractDependencies.emplace(dependencyContract, referencee);
		}
	}
}

void CompilerStack::findAndReportCyclicContractDependencies()
{
	// Cycles we found, used to avoid duplicate reports for the same reference
	std::set<ASTNode const*, ASTNode::CompareByID> foundCycles;

	for (Source const* source: m_sourceOrder)
	{
		if (!source->ast)
			continue;

		for (ContractDefinition const* contractDefinition: ASTNode::filteredNodes<ContractDefinition>(source->ast->nodes()))
		{
			util::CycleDetector<ContractDefinition> cycleDetector{[&](
				ContractDefinition const& _contract,
				util::CycleDetector<ContractDefinition>& _cycleDetector,
				size_t _depth
			)
			{
				// No specific reason for exactly that number, just a limit we're unlikely to hit.
				if (_depth >= 256)
					m_errorReporter.fatalTypeError(
						7864_error,
						_contract.location(),
						"Contract dependencies exhausting cyclic dependency validator"
					);

				for (auto& [dependencyContract, referencee]: _contract.annotation().contractDependencies)
					if (_cycleDetector.run(*dependencyContract))
						return;
			}};

			ContractDefinition const* cycle = cycleDetector.run(*contractDefinition);

			if (!cycle)
				continue;

			ASTNode const* referencee = contractDefinition->annotation().contractDependencies.at(cycle);

			if (foundCycles.find(referencee) != foundCycles.end())
				continue;

			SecondarySourceLocation secondaryLocation{};
			secondaryLocation.append("Referenced contract is here:"s, cycle->location());

			m_errorReporter.typeError(
				7813_error,
				referencee->location(),
				secondaryLocation,
				"Circular reference to contract bytecode either via \"new\" or \"type(...).creationCode\" / \"type(...).runtimeCode\"."
			);

			foundCycles.emplace(referencee);
		}
	}
}

void CompilerStack::setRemappings(std::vector<ImportRemapper::Remapping> _remappings)
{
	if (m_stackState >= ParsedAndImported)
		hypThrow(CompilerError, "Must set remappings before parsing.");
	for (auto const& remapping: _remappings)
		hypAssert(!remapping.prefix.empty(), "");
	m_importRemapper.setRemappings(std::move(_remappings));
}

void CompilerStack::setViaIR(bool _viaIR)
{
	if (m_stackState >= ParsedAndImported)
		hypThrow(CompilerError, "Must set viaIR before parsing.");
	m_viaIR = _viaIR;
}

void CompilerStack::setZVMVersion(langutil::ZVMVersion _version)
{
	if (m_stackState >= ParsedAndImported)
		hypThrow(CompilerError, "Must set ZVM version before parsing.");
	m_zvmVersion = _version;
}

void CompilerStack::setModelCheckerSettings(ModelCheckerSettings _settings)
{
	if (m_stackState >= ParsedAndImported)
		hypThrow(CompilerError, "Must set model checking settings before parsing.");
	m_modelCheckerSettings = _settings;
}

void CompilerStack::setLibraries(std::map<std::string, util::h160> const& _libraries)
{
	if (m_stackState >= ParsedAndImported)
		hypThrow(CompilerError, "Must set libraries before parsing.");
	m_libraries = _libraries;
}

void CompilerStack::setOptimiserSettings(bool _optimize, size_t _runs)
{
	OptimiserSettings settings = _optimize ? OptimiserSettings::standard() : OptimiserSettings::minimal();
	settings.expectedExecutionsPerDeployment = _runs;
	setOptimiserSettings(std::move(settings));
}

void CompilerStack::setOptimiserSettings(OptimiserSettings _settings)
{
	if (m_stackState >= ParsedAndImported)
		hypThrow(CompilerError, "Must set optimiser settings before parsing.");
	m_optimiserSettings = std::move(_settings);
}

void CompilerStack::setRevertStringBehaviour(RevertStrings _revertStrings)
{
	if (m_stackState >= ParsedAndImported)
		hypThrow(CompilerError, "Must set revert std::string settings before parsing.");
	hypUnimplementedAssert(_revertStrings != RevertStrings::VerboseDebug);
	m_revertStrings = _revertStrings;
}

void CompilerStack::useMetadataLiteralSources(bool _metadataLiteralSources)
{
	if (m_stackState >= ParsedAndImported)
		hypThrow(CompilerError, "Must set use literal sources before parsing.");
	m_metadataLiteralSources = _metadataLiteralSources;
}

void CompilerStack::setMetadataHash(MetadataHash _metadataHash)
{
	if (m_stackState >= ParsedAndImported)
		hypThrow(CompilerError, "Must set metadata hash before parsing.");
	m_metadataHash = _metadataHash;
}

void CompilerStack::selectDebugInfo(DebugInfoSelection _debugInfoSelection)
{
	if (m_stackState >= CompilationSuccessful)
		BOOST_THROW_EXCEPTION(CompilerError() << util::errinfo_comment("Must select debug info components before compilation."));
	m_debugInfoSelection = _debugInfoSelection;
}

void CompilerStack::addSMTLib2Response(h256 const& _hash, std::string const& _response)
{
	if (m_stackState >= ParsedAndImported)
		hypThrow(CompilerError, "Must add SMTLib2 responses before parsing.");
	m_smtlib2Responses[_hash] = _response;
}

void CompilerStack::reset(bool _keepSettings)
{
	m_stackState = Empty;
	m_sources.clear();
	m_smtlib2Responses.clear();
	m_unhandledSMTLib2Queries.clear();
	if (!_keepSettings)
	{
		m_importRemapper.clear();
		m_libraries.clear();
		m_viaIR = false;
		m_zvmVersion = langutil::ZVMVersion();
		m_modelCheckerSettings = ModelCheckerSettings{};
		m_generateIR = false;
		m_revertStrings = RevertStrings::Default;
		m_optimiserSettings = OptimiserSettings::minimal();
		m_metadataLiteralSources = false;
		m_metadataFormat = defaultMetadataFormat();
		m_metadataHash = MetadataHash::IPFS;
		m_stopAfter = State::CompilationSuccessful;
	}
	m_experimentalAnalysis.reset();
	m_globalContext.reset();
	m_sourceOrder.clear();
	m_contracts.clear();
	m_errorReporter.clear();
	TypeProvider::reset();
}

void CompilerStack::setSources(StringMap _sources)
{
	if (m_stackState == SourcesSet)
		hypThrow(CompilerError, "Cannot change sources once set.");
	if (m_stackState != Empty)
		hypThrow(CompilerError, "Must set sources before parsing.");
	for (auto source: _sources)
		m_sources[source.first].charStream = std::make_unique<CharStream>(/*content*/std::move(source.second), /*name*/source.first);
	m_stackState = SourcesSet;
}

bool CompilerStack::parse()
{
	if (m_stackState != SourcesSet)
		hypThrow(CompilerError, "Must call parse only after the SourcesSet state.");
	m_errorReporter.clear();

	if (SemVerVersion{std::string(VersionString)}.isPrerelease())
		m_errorReporter.warning(3805_error, "This is a pre-release compiler version, please do not use it in production.");

	Parser parser{m_errorReporter, m_zvmVersion};

	std::vector<std::string> sourcesToParse;
	for (auto const& s: m_sources)
		sourcesToParse.push_back(s.first);

	for (size_t i = 0; i < sourcesToParse.size(); ++i)
	{
		std::string const& path = sourcesToParse[i];
		Source& source = m_sources[path];
		source.ast = parser.parse(*source.charStream);
		if (!source.ast)
			hypAssert(Error::containsErrors(m_errorReporter.errors()), "Parser returned null but did not report error.");
		else
		{
			source.ast->annotation().path = path;

			for (auto const& import: ASTNode::filteredNodes<ImportDirective>(source.ast->nodes()))
			{
				hypAssert(!import->path().empty(), "Import path cannot be empty.");
				// Check whether the import directive is for the standard library,
				// and if yes, add specified file to source units to be parsed.
				auto it = stdlib::sources.find(import->path());
				if (it != stdlib::sources.end())
				{
					auto [name, content] = *it;
					m_sources[name].charStream = std::make_unique<CharStream>(content, name);
					sourcesToParse.push_back(name);
				}

				// The current value of `path` is the absolute path as seen from this source file.
				// We first have to apply remappings before we can store the actual absolute path
				// as seen globally.
				import->annotation().absolutePath = applyRemapping(util::absolutePath(
					import->path(),
					path
				), path);
			}

			if (m_stopAfter >= ParsedAndImported)
				for (auto const& newSource: loadMissingSources(*source.ast))
				{
					std::string const& newPath = newSource.first;
					std::string const& newContents = newSource.second;
					m_sources[newPath].charStream = std::make_shared<CharStream>(newContents, newPath);
					sourcesToParse.push_back(newPath);
				}
		}
	}

	if (Error::containsErrors(m_errorReporter.errors()))
		return false;

	m_stackState = (m_stopAfter <= Parsed ? Parsed : ParsedAndImported);
	storeContractDefinitions();
	return true;
}

void CompilerStack::importASTs(std::map<std::string, Json::Value> const& _sources)
{
	if (m_stackState != Empty)
		hypThrow(CompilerError, "Must call importASTs only before the SourcesSet state.");
	std::map<std::string, ASTPointer<SourceUnit>> reconstructedSources = ASTJsonImporter(m_zvmVersion).jsonToSourceUnit(_sources);
	for (auto& src: reconstructedSources)
	{
		std::string const& path = src.first;
		Source source;
		source.ast = src.second;
		source.charStream = std::make_shared<CharStream>(
			util::jsonCompactPrint(_sources.at(src.first)),
			src.first,
			true // imported from AST
		);
		m_sources[path] = std::move(source);
	}
	m_stackState = ParsedAndImported;
	m_compilationSourceType = CompilationSourceType::HyperionAST;

	storeContractDefinitions();
}

bool CompilerStack::analyze()
{
	if (m_stackState != ParsedAndImported)
		hypThrow(CompilerError, "Must call analyze only after parsing was successful.");

	if (!resolveImports())
		return false;

	for (Source const* source: m_sourceOrder)
		if (source->ast)
			Scoper::assignScopes(*source->ast);

	bool noErrors = true;

	try
	{
		bool experimentalHyperion = !m_sourceOrder.empty() && m_sourceOrder.front()->ast->experimentalHyperion();

		SyntaxChecker syntaxChecker(m_errorReporter, m_optimiserSettings.runYulOptimiser);
		for (Source const* source: m_sourceOrder)
			if (source->ast && !syntaxChecker.checkSyntax(*source->ast))
				noErrors = false;

		m_globalContext = std::make_shared<GlobalContext>();
		// We need to keep the same resolver during the whole process.
		NameAndTypeResolver resolver(*m_globalContext, m_zvmVersion, m_errorReporter);
		for (Source const* source: m_sourceOrder)
			if (source->ast && !resolver.registerDeclarations(*source->ast))
				return false;

		std::map<std::string, SourceUnit const*> sourceUnitsByName;
		for (auto& source: m_sources)
			sourceUnitsByName[source.first] = source.second.ast.get();
		for (Source const* source: m_sourceOrder)
			if (source->ast && !resolver.performImports(*source->ast, sourceUnitsByName))
				return false;

		resolver.warnHomonymDeclarations();

		DocStringTagParser docStringTagParser(m_errorReporter);
		for (Source const* source: m_sourceOrder)
			if (source->ast && !docStringTagParser.parseDocStrings(*source->ast))
				noErrors = false;

		// Requires DocStringTagParser
		for (Source const* source: m_sourceOrder)
			if (source->ast && !resolver.resolveNamesAndTypes(*source->ast))
				return false;

		if (experimentalHyperion)
		{
			if (!analyzeExperimental())
				noErrors = false;
		}
		else if (!analyzeLegacy(noErrors))
			noErrors = false;
	}
	catch (FatalError const&)
	{
		if (m_errorReporter.errors().empty())
			throw; // Something is weird here, rather throw again.
		noErrors = false;
	}

	if (!noErrors)
		return false;

	m_stackState = AnalysisSuccessful;
	return true;
}


bool CompilerStack::analyzeLegacy(bool _noErrorsSoFar)
{
	bool noErrors = _noErrorsSoFar;

	DeclarationTypeChecker declarationTypeChecker(m_errorReporter, m_zvmVersion);
	for (Source const* source: m_sourceOrder)
		if (source->ast && !declarationTypeChecker.check(*source->ast))
			return false;

	// Requires DeclarationTypeChecker to have run
	DocStringTagParser docStringTagParser(m_errorReporter);
	for (Source const* source: m_sourceOrder)
		if (source->ast && !docStringTagParser.validateDocStringsUsingTypes(*source->ast))
			noErrors = false;

	// Next, we check inheritance, overrides, function collisions and other things at
	// contract or function level.
	// This also calculates whether a contract is abstract, which is needed by the
	// type checker.
	ContractLevelChecker contractLevelChecker(m_errorReporter);

	for (Source const* source: m_sourceOrder)
		if (auto sourceAst = source->ast)
			noErrors = contractLevelChecker.check(*sourceAst);

	// Now we run full type checks that go down to the expression level. This
	// cannot be done earlier, because we need cross-contract types and information
	// about whether a contract is abstract for the `new` expression.
	// This populates the `type` annotation for all expressions.
	//
	// Note: this does not resolve overloaded functions. In order to do that, types of arguments are needed,
	// which is only done one step later.
	TypeChecker typeChecker(m_zvmVersion, m_errorReporter);
	for (Source const* source: m_sourceOrder)
		if (source->ast && !typeChecker.checkTypeRequirements(*source->ast))
			noErrors = false;

	if (noErrors)
	{
		// Requires ContractLevelChecker and TypeChecker
		DocStringAnalyser docStringAnalyser(m_errorReporter);
		for (Source const* source: m_sourceOrder)
			if (source->ast && !docStringAnalyser.analyseDocStrings(*source->ast))
				noErrors = false;
	}

	if (noErrors)
	{
		// Checks that can only be done when all types of all AST nodes are known.
		PostTypeChecker postTypeChecker(m_errorReporter);
		for (Source const* source: m_sourceOrder)
			if (source->ast && !postTypeChecker.check(*source->ast))
				noErrors = false;
		if (!postTypeChecker.finalize())
			noErrors = false;
	}

	// Create & assign callgraphs and check for contract dependency cycles
	if (noErrors)
	{
		createAndAssignCallGraphs();
		annotateInternalFunctionIDs();
		findAndReportCyclicContractDependencies();
	}

	if (noErrors)
		for (Source const* source: m_sourceOrder)
			if (source->ast && !PostTypeContractLevelChecker{m_errorReporter}.check(*source->ast))
				noErrors = false;

	// Check that immutable variables are never read in c'tors and assigned
	// exactly once
	if (noErrors)
		for (Source const* source: m_sourceOrder)
			if (source->ast)
				for (ASTPointer<ASTNode> const& node: source->ast->nodes())
					if (ContractDefinition* contract = dynamic_cast<ContractDefinition*>(node.get()))
						ImmutableValidator(m_errorReporter, *contract).analyze();

	if (noErrors)
	{
		// Control flow graph generator and analyzer. It can check for issues such as
		// variable is used before it is assigned to.
		CFG cfg(m_errorReporter);
		for (Source const* source: m_sourceOrder)
			if (source->ast && !cfg.constructFlow(*source->ast))
				noErrors = false;

		if (noErrors)
		{
			ControlFlowRevertPruner pruner(cfg);
			pruner.run();

			ControlFlowAnalyzer controlFlowAnalyzer(cfg, m_errorReporter);
			if (!controlFlowAnalyzer.run())
				noErrors = false;
		}
	}

	if (noErrors)
	{
		// Checks for common mistakes. Only generates warnings.
		StaticAnalyzer staticAnalyzer(m_errorReporter);
		for (Source const* source: m_sourceOrder)
			if (source->ast && !staticAnalyzer.analyze(*source->ast))
				noErrors = false;
	}

	if (noErrors)
	{
		// Check for state mutability in every function.
		std::vector<ASTPointer<ASTNode>> ast;
		for (Source const* source: m_sourceOrder)
			if (source->ast)
				ast.push_back(source->ast);

		if (!ViewPureChecker(ast, m_errorReporter).check())
			noErrors = false;
	}

	if (noErrors)
	{
		// Run SMTChecker

		auto allSources = util::applyMap(m_sourceOrder, [](Source const* _source) { return _source->ast; });
		if (ModelChecker::isPragmaPresent(allSources))
			m_modelCheckerSettings.engine = ModelCheckerEngine::All();

		// m_modelCheckerSettings is spread to engines and solver interfaces,
		// so we need to check whether the enabled ones are available before building the classes.
		if (m_modelCheckerSettings.engine.any())
			m_modelCheckerSettings.solvers = ModelChecker::checkRequestedSolvers(m_modelCheckerSettings.solvers, m_errorReporter);

		ModelChecker modelChecker(m_errorReporter, *this, m_smtlib2Responses, m_modelCheckerSettings, m_readFile);
		modelChecker.checkRequestedSourcesAndContracts(allSources);
		for (Source const* source: m_sourceOrder)
			if (source->ast)
				modelChecker.analyze(*source->ast);
		m_unhandledSMTLib2Queries += modelChecker.unhandledQueries();
	}

	return noErrors;
}

bool CompilerStack::analyzeExperimental()
{
	hypAssert(!m_experimentalAnalysis);
	m_experimentalAnalysis = std::make_unique<experimental::Analysis>(m_errorReporter);
	std::vector<std::shared_ptr<SourceUnit const>> sourceAsts;
	for (Source const* source: m_sourceOrder)
		if (source->ast)
			sourceAsts.emplace_back(source->ast);
	return m_experimentalAnalysis->check(sourceAsts);
}

bool CompilerStack::parseAndAnalyze(State _stopAfter)
{
	m_stopAfter = _stopAfter;

	bool success = parse();
	if (m_stackState >= m_stopAfter)
		return success;
	if (success)
		success = analyze();
	return success;
}

bool CompilerStack::isRequestedSource(std::string const& _sourceName) const
{
	return
		m_requestedContractNames.empty() ||
		m_requestedContractNames.count("") ||
		m_requestedContractNames.count(_sourceName);
}

bool CompilerStack::isRequestedContract(ContractDefinition const& _contract) const
{
	/// In case nothing was specified in outputSelection.
	if (m_requestedContractNames.empty())
		return true;

	for (auto const& key: std::vector<std::string>{"", _contract.sourceUnitName()})
	{
		auto const& it = m_requestedContractNames.find(key);
		if (it != m_requestedContractNames.end())
			if (it->second.count(_contract.name()) || it->second.count(""))
				return true;
	}

	return false;
}

bool CompilerStack::compile(State _stopAfter)
{
	m_stopAfter = _stopAfter;
	if (m_stackState < AnalysisSuccessful)
		if (!parseAndAnalyze(_stopAfter))
			return false;

	if (m_stackState >= m_stopAfter)
		return true;

	// Only compile contracts individually which have been requested.
	std::map<ContractDefinition const*, std::shared_ptr<Compiler const>> otherCompilers;

	for (Source const* source: m_sourceOrder)
		for (ASTPointer<ASTNode> const& node: source->ast->nodes())
			if (auto contract = dynamic_cast<ContractDefinition const*>(node.get()))
				if (isRequestedContract(*contract))
				{
					try
					{
						if (m_viaIR || m_generateIR)
							generateIR(*contract);
						if (m_generateZvmBytecode)
						{
							if (m_viaIR)
								generateZVMFromIR(*contract);
							else
							{
								if (m_experimentalAnalysis)
									hypThrow(CompilerError, "Legacy codegen after experimental analysis is unsupported.");
								compileContract(*contract, otherCompilers);
							}
						}
					}
					catch (Error const& _error)
					{
						if (_error.type() != Error::Type::CodeGenerationError)
							throw;
						m_errorReporter.error(_error.errorId(), _error.type(), SourceLocation(), _error.what());
						return false;
					}
					catch (UnimplementedFeatureError const& _unimplementedError)
					{
						if (
							SourceLocation const* sourceLocation =
							boost::get_error_info<langutil::errinfo_sourceLocation>(_unimplementedError)
						)
						{
							std::string const* comment = _unimplementedError.comment();
							m_errorReporter.error(
								1834_error,
								Error::Type::CodeGenerationError,
								*sourceLocation,
								fmt::format(
									"Unimplemented feature error {} in {}",
									(comment && !comment->empty()) ? ": " + *comment : "",
									_unimplementedError.lineInfo()
								)
							);
							return false;
						}
						else
							throw;
					}
				}
	m_stackState = CompilationSuccessful;
	this->link();
	return true;
}

void CompilerStack::link()
{
	hypAssert(m_stackState >= CompilationSuccessful, "");
	for (auto& contract: m_contracts)
	{
		contract.second.object.link(m_libraries);
		contract.second.runtimeObject.link(m_libraries);
	}
}

std::vector<std::string> CompilerStack::contractNames() const
{
	if (m_stackState < Parsed)
		hypThrow(CompilerError, "Parsing was not successful.");
	std::vector<std::string> contractNames;
	for (auto const& contract: m_contracts)
		contractNames.push_back(contract.first);
	return contractNames;
}

std::string const CompilerStack::lastContractName(std::optional<std::string> const& _sourceName) const
{
	if (m_stackState < AnalysisSuccessful)
		hypThrow(CompilerError, "Parsing was not successful.");
	// try to find some user-supplied contract
	std::string contractName;
	for (auto const& it: m_sources)
		if (_sourceName.value_or(it.first) == it.first)
			for (auto const* contract: ASTNode::filteredNodes<ContractDefinition>(it.second.ast->nodes()))
				contractName = contract->fullyQualifiedName();
	return contractName;
}

zvmasm::AssemblyItems const* CompilerStack::assemblyItems(std::string const& _contractName) const
{
	if (m_stackState != CompilationSuccessful)
		hypThrow(CompilerError, "Compilation was not successful.");

	Contract const& currentContract = contract(_contractName);
	return currentContract.zvmAssembly ? &currentContract.zvmAssembly->items() : nullptr;
}

zvmasm::AssemblyItems const* CompilerStack::runtimeAssemblyItems(std::string const& _contractName) const
{
	if (m_stackState != CompilationSuccessful)
		hypThrow(CompilerError, "Compilation was not successful.");

	Contract const& currentContract = contract(_contractName);
	return currentContract.zvmRuntimeAssembly ? &currentContract.zvmRuntimeAssembly->items() : nullptr;
}

Json::Value CompilerStack::generatedSources(std::string const& _contractName, bool _runtime) const
{
	if (m_stackState != CompilationSuccessful)
		hypThrow(CompilerError, "Compilation was not successful.");

	Contract const& c = contract(_contractName);
	util::LazyInit<Json::Value const> const& sources =
		_runtime ?
		c.runtimeGeneratedSources :
		c.generatedSources;
	return sources.init([&]{
		Json::Value sources{Json::arrayValue};
		// If there is no compiler, then no bytecode was generated and thus no
		// sources were generated (or we compiled "via IR").
		if (c.compiler)
		{
			hypAssert(!m_viaIR, "");
			std::string source =
				_runtime ?
				c.compiler->runtimeGeneratedYulUtilityCode() :
				c.compiler->generatedYulUtilityCode();
			if (!source.empty())
			{
				std::string sourceName = CompilerContext::yulUtilityFileName();
				unsigned sourceIndex = sourceIndices()[sourceName];
				ErrorList errors;
				ErrorReporter errorReporter(errors);
				CharStream charStream(source, sourceName);
				yul::ZVMDialect const& dialect = yul::ZVMDialect::strictAssemblyForZVM(m_zvmVersion);
				std::shared_ptr<yul::Block> parserResult = yul::Parser{errorReporter, dialect}.parse(charStream);
				hypAssert(parserResult, "");
				sources[0]["ast"] = yul::AsmJsonConverter{sourceIndex}(*parserResult);
				sources[0]["name"] = sourceName;
				sources[0]["id"] = sourceIndex;
				sources[0]["language"] = "Yul";
				sources[0]["contents"] = std::move(source);
			}
		}
		return sources;
	});
}

std::string const* CompilerStack::sourceMapping(std::string const& _contractName) const
{
	if (m_stackState != CompilationSuccessful)
		hypThrow(CompilerError, "Compilation was not successful.");

	Contract const& c = contract(_contractName);
	if (!c.sourceMapping)
	{
		if (auto items = assemblyItems(_contractName))
			c.sourceMapping.emplace(zvmasm::AssemblyItem::computeSourceMapping(*items, sourceIndices()));
	}
	return c.sourceMapping ? &*c.sourceMapping : nullptr;
}

std::string const* CompilerStack::runtimeSourceMapping(std::string const& _contractName) const
{
	if (m_stackState != CompilationSuccessful)
		hypThrow(CompilerError, "Compilation was not successful.");

	Contract const& c = contract(_contractName);
	if (!c.runtimeSourceMapping)
	{
		if (auto items = runtimeAssemblyItems(_contractName))
			c.runtimeSourceMapping.emplace(
				zvmasm::AssemblyItem::computeSourceMapping(*items, sourceIndices())
			);
	}
	return c.runtimeSourceMapping ? &*c.runtimeSourceMapping : nullptr;
}

std::string const CompilerStack::filesystemFriendlyName(std::string const& _contractName) const
{
	if (m_stackState < AnalysisSuccessful)
		hypThrow(CompilerError, "No compiled contracts found.");

	// Look up the contract (by its fully-qualified name)
	Contract const& matchContract = m_contracts.at(_contractName);
	// Check to see if it could collide on name
	for (auto const& contract: m_contracts)
	{
		if (contract.second.contract->name() == matchContract.contract->name() &&
				contract.second.contract != matchContract.contract)
		{
			// If it does, then return its fully-qualified name, made fs-friendly
			std::string friendlyName = boost::algorithm::replace_all_copy(_contractName, "/", "_");
			boost::algorithm::replace_all(friendlyName, ":", "_");
			boost::algorithm::replace_all(friendlyName, ".", "_");
			return friendlyName;
		}
	}
	// If no collision, return the contract's name
	return matchContract.contract->name();
}

std::string const& CompilerStack::yulIR(std::string const& _contractName) const
{
	if (m_stackState != CompilationSuccessful)
		hypThrow(CompilerError, "Compilation was not successful.");

	return contract(_contractName).yulIR;
}

Json::Value const& CompilerStack::yulIRAst(std::string const& _contractName) const
{
	if (m_stackState != CompilationSuccessful)
		hypThrow(CompilerError, "Compilation was not successful.");

	return contract(_contractName).yulIRAst;
}

std::string const& CompilerStack::yulIROptimized(std::string const& _contractName) const
{
	if (m_stackState != CompilationSuccessful)
		hypThrow(CompilerError, "Compilation was not successful.");

	return contract(_contractName).yulIROptimized;
}

Json::Value const& CompilerStack::yulIROptimizedAst(std::string const& _contractName) const
{
	if (m_stackState != CompilationSuccessful)
		hypThrow(CompilerError, "Compilation was not successful.");

	return contract(_contractName).yulIROptimizedAst;
}

zvmasm::LinkerObject const& CompilerStack::object(std::string const& _contractName) const
{
	if (m_stackState != CompilationSuccessful)
		hypThrow(CompilerError, "Compilation was not successful.");

	return contract(_contractName).object;
}

zvmasm::LinkerObject const& CompilerStack::runtimeObject(std::string const& _contractName) const
{
	if (m_stackState != CompilationSuccessful)
		hypThrow(CompilerError, "Compilation was not successful.");

	return contract(_contractName).runtimeObject;
}

/// TODO: cache this std::string
std::string CompilerStack::assemblyString(std::string const& _contractName, StringMap const& _sourceCodes) const
{
	if (m_stackState != CompilationSuccessful)
		hypThrow(CompilerError, "Compilation was not successful.");

	Contract const& currentContract = contract(_contractName);
	if (currentContract.zvmAssembly)
		return currentContract.zvmAssembly->assemblyString(m_debugInfoSelection, _sourceCodes);
	else
		return std::string();
}

/// TODO: cache the JSON
Json::Value CompilerStack::assemblyJSON(std::string const& _contractName) const
{
	if (m_stackState != CompilationSuccessful)
		hypThrow(CompilerError, "Compilation was not successful.");

	Contract const& currentContract = contract(_contractName);
	if (currentContract.zvmAssembly)
		return currentContract.zvmAssembly->assemblyJSON(sourceIndices());
	else
		return Json::Value();
}

std::vector<std::string> CompilerStack::sourceNames() const
{
	return ranges::to<std::vector>(m_sources | ranges::views::keys);
}

std::map<std::string, unsigned> CompilerStack::sourceIndices() const
{
	std::map<std::string, unsigned> indices;
	unsigned index = 0;
	for (auto const& s: m_sources)
		indices[s.first] = index++;
	hypAssert(!indices.count(CompilerContext::yulUtilityFileName()), "");
	indices[CompilerContext::yulUtilityFileName()] = index++;
	return indices;
}

Json::Value const& CompilerStack::contractABI(std::string const& _contractName) const
{
	if (m_stackState < AnalysisSuccessful)
		hypThrow(CompilerError, "Analysis was not successful.");

	return contractABI(contract(_contractName));
}

Json::Value const& CompilerStack::contractABI(Contract const& _contract) const
{
	if (m_stackState < AnalysisSuccessful)
		hypThrow(CompilerError, "Analysis was not successful.");

	hypAssert(_contract.contract, "");

	return _contract.abi.init([&]{ return ABI::generate(*_contract.contract); });
}

Json::Value const& CompilerStack::storageLayout(std::string const& _contractName) const
{
	if (m_stackState < AnalysisSuccessful)
		hypThrow(CompilerError, "Analysis was not successful.");

	return storageLayout(contract(_contractName));
}

Json::Value const& CompilerStack::storageLayout(Contract const& _contract) const
{
	if (m_stackState < AnalysisSuccessful)
		hypThrow(CompilerError, "Analysis was not successful.");

	hypAssert(_contract.contract, "");

	return _contract.storageLayout.init([&]{ return StorageLayout().generate(*_contract.contract); });
}

Json::Value const& CompilerStack::natspecUser(std::string const& _contractName) const
{
	if (m_stackState < AnalysisSuccessful)
		hypThrow(CompilerError, "Analysis was not successful.");

	return natspecUser(contract(_contractName));
}

Json::Value const& CompilerStack::natspecUser(Contract const& _contract) const
{
	if (m_stackState < AnalysisSuccessful)
		hypThrow(CompilerError, "Analysis was not successful.");

	hypAssert(_contract.contract, "");

	return _contract.userDocumentation.init([&]{ return Natspec::userDocumentation(*_contract.contract); });
}

Json::Value const& CompilerStack::natspecDev(std::string const& _contractName) const
{
	if (m_stackState < AnalysisSuccessful)
		hypThrow(CompilerError, "Analysis was not successful.");

	return natspecDev(contract(_contractName));
}

Json::Value const& CompilerStack::natspecDev(Contract const& _contract) const
{
	if (m_stackState < AnalysisSuccessful)
		hypThrow(CompilerError, "Analysis was not successful.");

	hypAssert(_contract.contract, "");

	return _contract.devDocumentation.init([&]{ return Natspec::devDocumentation(*_contract.contract); });
}

Json::Value CompilerStack::interfaceSymbols(std::string const& _contractName) const
{
	if (m_stackState < AnalysisSuccessful)
		hypThrow(CompilerError, "Analysis was not successful.");

	Json::Value interfaceSymbols(Json::objectValue);
	// Always have a methods object
	interfaceSymbols["methods"] = Json::objectValue;

	for (auto const& it: contractDefinition(_contractName).interfaceFunctions())
		interfaceSymbols["methods"][it.second->externalSignature()] = it.first.hex();
	for (ErrorDefinition const* error: contractDefinition(_contractName).interfaceErrors())
	{
		std::string signature = error->functionType(true)->externalSignature();
		interfaceSymbols["errors"][signature] = util::toHex(toCompactBigEndian(util::selectorFromSignatureU32(signature), 4));
	}

	for (EventDefinition const* event: ranges::concat_view(
		contractDefinition(_contractName).definedInterfaceEvents(),
		contractDefinition(_contractName).usedInterfaceEvents()
	))
		if (!event->isAnonymous())
		{
			std::string signature = event->functionType(true)->externalSignature();
			interfaceSymbols["events"][signature] = toHex(u256(h256::Arith(util::keccak256(signature))));
		}

	return interfaceSymbols;
}

bytes CompilerStack::cborMetadata(std::string const& _contractName, bool _forIR) const
{
	if (m_stackState < AnalysisSuccessful)
		hypThrow(CompilerError, "Analysis was not successful.");

	return createCBORMetadata(contract(_contractName), _forIR);
}

std::string const& CompilerStack::metadata(Contract const& _contract) const
{
	if (m_stackState < AnalysisSuccessful)
		hypThrow(CompilerError, "Analysis was not successful.");

	hypAssert(_contract.contract, "");

	return _contract.metadata.init([&]{ return createMetadata(_contract, m_viaIR); });
}

CharStream const& CompilerStack::charStream(std::string const& _sourceName) const
{
	if (m_stackState < SourcesSet)
		hypThrow(CompilerError, "No sources set.");

	hypAssert(source(_sourceName).charStream, "");

	return *source(_sourceName).charStream;
}

SourceUnit const& CompilerStack::ast(std::string const& _sourceName) const
{
	if (m_stackState < Parsed)
		hypThrow(CompilerError, "Parsing not yet performed.");
	if (!source(_sourceName).ast)
		hypThrow(CompilerError, "Parsing was not successful.");

	return *source(_sourceName).ast;
}

ContractDefinition const& CompilerStack::contractDefinition(std::string const& _contractName) const
{
	if (m_stackState < AnalysisSuccessful)
		hypThrow(CompilerError, "Analysis was not successful.");

	return *contract(_contractName).contract;
}

size_t CompilerStack::functionEntryPoint(
	std::string const& _contractName,
	FunctionDefinition const& _function
) const
{
	if (m_stackState != CompilationSuccessful)
		hypThrow(CompilerError, "Compilation was not successful.");

	for (auto&& [name, data]: contract(_contractName).runtimeObject.functionDebugData)
		if (data.sourceID == _function.id())
			if (data.instructionIndex)
				return *data.instructionIndex;
	return 0;
}

h256 const& CompilerStack::Source::keccak256() const
{
	if (keccak256HashCached == h256{})
		keccak256HashCached = util::keccak256(charStream->source());
	return keccak256HashCached;
}

h256 const& CompilerStack::Source::swarmHash() const
{
	if (swarmHashCached == h256{})
		swarmHashCached = util::bzzr1Hash(charStream->source());
	return swarmHashCached;
}

std::string const& CompilerStack::Source::ipfsUrl() const
{
	if (ipfsUrlCached.empty())
		ipfsUrlCached = "dweb:/ipfs/" + util::ipfsHashBase58(charStream->source());
	return ipfsUrlCached;
}

StringMap CompilerStack::loadMissingSources(SourceUnit const& _ast)
{
	hypAssert(m_stackState < ParsedAndImported, "");
	StringMap newSources;
	try
	{
		for (auto const& node: _ast.nodes())
			if (ImportDirective const* import = dynamic_cast<ImportDirective*>(node.get()))
			{
				std::string const& importPath = *import->annotation().absolutePath;

				if (m_sources.count(importPath) || newSources.count(importPath))
					continue;

				ReadCallback::Result result{false, std::string("File not supplied initially.")};
				if (m_readFile)
					result = m_readFile(ReadCallback::kindString(ReadCallback::Kind::ReadFile), importPath);

				if (result.success)
					newSources[importPath] = result.responseOrErrorMessage;
				else
				{
					m_errorReporter.parserError(
						6275_error,
						import->location(),
						std::string("Source \"" + importPath + "\" not found: " + result.responseOrErrorMessage)
					);
					continue;
				}
			}
	}
	catch (FatalError const&)
	{
		hypAssert(m_errorReporter.hasErrors(), "");
	}
	return newSources;
}

std::string CompilerStack::applyRemapping(std::string const& _path, std::string const& _context)
{
	hypAssert(m_stackState < ParsedAndImported, "");
	return m_importRemapper.apply(_path, _context);
}

bool CompilerStack::resolveImports()
{
	hypAssert(m_stackState == ParsedAndImported, "");

	// topological sorting (depth first search) of the import graph, cutting potential cycles
	std::vector<Source const*> sourceOrder;
	std::set<Source const*> sourcesSeen;

	std::function<void(Source const*)> toposort = [&](Source const* _source)
	{
		if (sourcesSeen.count(_source))
			return;
		sourcesSeen.insert(_source);
		hypAssert(_source->ast);
		for (ASTPointer<ASTNode> const& node: _source->ast->nodes())
			if (ImportDirective const* import = dynamic_cast<ImportDirective*>(node.get()))
			{
				std::string const& path = *import->annotation().absolutePath;
				hypAssert(m_sources.count(path), "");
				import->annotation().sourceUnit = m_sources[path].ast.get();
				toposort(&m_sources[path]);
			}
		sourceOrder.push_back(_source);
	};

	std::vector<PragmaDirective const*> experimentalPragmaDirectives;
	for (auto const& sourcePair: m_sources)
	{
		if (isRequestedSource(sourcePair.first))
			toposort(&sourcePair.second);
		if (sourcePair.second.ast && sourcePair.second.ast->experimentalHyperion())
			for (ASTPointer<ASTNode> const& node: sourcePair.second.ast->nodes())
				if (PragmaDirective const* pragma = dynamic_cast<PragmaDirective*>(node.get()))
					if (pragma->literals().size() >=2 && pragma->literals()[0] == "experimental" && pragma->literals()[1] == "hyperion")
					{
						experimentalPragmaDirectives.push_back(pragma);
						break;
					}
	}

	if (!experimentalPragmaDirectives.empty() && experimentalPragmaDirectives.size() != m_sources.size())
	{
		for (auto &&pragma: experimentalPragmaDirectives)
			m_errorReporter.parserError(
					2141_error,
					pragma->location(),
					"File declares \"pragma experimental hyperion\". If you want to enable the experimental mode, all source units must include the pragma."
			);
		return false;
	}

	swap(m_sourceOrder, sourceOrder);
	return true;
}

void CompilerStack::storeContractDefinitions()
{
	for (auto const& pair: m_sources)
		if (pair.second.ast)
			for (
				ContractDefinition const* contract:
				ASTNode::filteredNodes<ContractDefinition>(pair.second.ast->nodes())
			)
			{
				std::string fullyQualifiedName = *pair.second.ast->annotation().path + ":" + contract->name();
				// Note that we now reference contracts by their fully qualified names, and
				// thus contracts can only conflict if declared in the same source file. This
				// should already cause a double-declaration error elsewhere.
				if (!m_contracts.count(fullyQualifiedName))
					m_contracts[fullyQualifiedName].contract = contract;
			}
}

void CompilerStack::annotateInternalFunctionIDs()
{
	for (Source const* source: m_sourceOrder)
	{
		if (!source->ast)
			continue;

		for (ContractDefinition const* contract: ASTNode::filteredNodes<ContractDefinition>(source->ast->nodes()))
		{
			uint64_t internalFunctionID = 1;
			ContractDefinitionAnnotation& annotation = contract->annotation();

			if (auto const* deployTimeInternalDispatch = util::valueOrNullptr((*annotation.deployedCallGraph)->edges, CallGraph::SpecialNode::InternalDispatch))
				for (auto const& node: *deployTimeInternalDispatch)
					if (auto const* callable = std::get_if<CallableDeclaration const*>(&node))
						if (auto const* function = dynamic_cast<FunctionDefinition const*>(*callable))
						{
							hypAssert(contract->annotation().internalFunctionIDs.count(function) == 0);
							contract->annotation().internalFunctionIDs[function] = internalFunctionID++;
						}
			if (auto const* creationTimeInternalDispatch = util::valueOrNullptr((*annotation.creationCallGraph)->edges, CallGraph::SpecialNode::InternalDispatch))
				for (auto const& node: *creationTimeInternalDispatch)
					if (auto const* callable = std::get_if<CallableDeclaration const*>(&node))
						if (auto const* function = dynamic_cast<FunctionDefinition const*>(*callable))
							// Make sure the function already got an ID since it also occurs in the deploy-time internal dispatch.
							hypAssert(contract->annotation().internalFunctionIDs.count(function) != 0);
		}
	}
}

namespace
{
bool onlySafeExperimentalFeaturesActivated(std::set<ExperimentalFeature> const& features)
{
	for (auto const feature: features)
		if (!ExperimentalFeatureWithoutWarning.count(feature))
			return false;
	return true;
}
}

void CompilerStack::assembleYul(
	ContractDefinition const& _contract,
	std::shared_ptr<zvmasm::Assembly> _assembly,
	std::shared_ptr<zvmasm::Assembly> _runtimeAssembly
)
{
	hypAssert(m_stackState >= AnalysisSuccessful, "");

	Contract& compiledContract = m_contracts.at(_contract.fullyQualifiedName());

	compiledContract.zvmAssembly = _assembly;
	hypAssert(compiledContract.zvmAssembly, "");
	try
	{
		// Assemble deployment (incl. runtime)  object.
		compiledContract.object = compiledContract.zvmAssembly->assemble();
	}
	catch (zvmasm::AssemblyException const&)
	{
		hypAssert(false, "Assembly exception for bytecode");
	}
	hypAssert(compiledContract.object.immutableReferences.empty(), "Leftover immutables.");

	compiledContract.zvmRuntimeAssembly = _runtimeAssembly;
	hypAssert(compiledContract.zvmRuntimeAssembly, "");
	try
	{
		// Assemble runtime object.
		compiledContract.runtimeObject = compiledContract.zvmRuntimeAssembly->assemble();
	}
	catch (zvmasm::AssemblyException const&)
	{
		hypAssert(false, "Assembly exception for deployed bytecode");
	}

	// Throw a warning if EIP-170 limits are exceeded:
	//   If contract creation returns data with length greater than 0x6000 (2^14 + 2^13) bytes,
	//   contract creation fails with an out of gas error.
	if (
		compiledContract.runtimeObject.bytecode.size() > 0x6000
	)
		m_errorReporter.warning(
			5574_error,
			_contract.location(),
			"Contract code size is "s +
			std::to_string(compiledContract.runtimeObject.bytecode.size()) +
			" bytes and exceeds 24576 bytes. "
			"This contract may not be deployable on Mainnet. "
			"Consider enabling the optimizer (with a low \"runs\" value!), "
			"turning off revert strings, or using libraries."
		);

	// Throw a warning if EIP-3860 limits are exceeded:
	//   If initcode is larger than 0xC000 bytes (twice the runtime code limit),
	//   then contract creation fails with an out of gas error.
	if (
		compiledContract.object.bytecode.size() > 0xC000
	)
		m_errorReporter.warning(
			3860_error,
			_contract.location(),
			"Contract initcode size is "s +
			std::to_string(compiledContract.object.bytecode.size()) +
			" bytes and exceeds 49152 bytes. "
			"This contract may not be deployable on Mainnet. "
			"Consider enabling the optimizer (with a low \"runs\" value!), "
			"turning off revert strings, or using libraries."
		);
}

void CompilerStack::compileContract(
	ContractDefinition const& _contract,
	std::map<ContractDefinition const*, std::shared_ptr<Compiler const>>& _otherCompilers
)
{
	hypAssert(!m_viaIR, "");
	hypAssert(m_stackState >= AnalysisSuccessful, "");

	if (_otherCompilers.count(&_contract))
		return;

	for (auto const& [dependency, referencee]: _contract.annotation().contractDependencies)
		compileContract(*dependency, _otherCompilers);

	if (!_contract.canBeDeployed())
		return;

	Contract& compiledContract = m_contracts.at(_contract.fullyQualifiedName());

	std::shared_ptr<Compiler> compiler = std::make_shared<Compiler>(m_zvmVersion, m_revertStrings, m_optimiserSettings);
	compiledContract.compiler = compiler;

	hypAssert(!m_viaIR, "");
	bytes cborEncodedMetadata = createCBORMetadata(compiledContract, /* _forIR */ false);

	try
	{
		// Run optimiser and compile the contract.
		compiler->compileContract(_contract, _otherCompilers, cborEncodedMetadata);
	}
	catch(zvmasm::OptimizerException const&)
	{
		hypAssert(false, "Optimizer exception during compilation");
	}

	_otherCompilers[compiledContract.contract] = compiler;

	assembleYul(_contract, compiler->assemblyPtr(), compiler->runtimeAssemblyPtr());
}

void CompilerStack::generateIR(ContractDefinition const& _contract)
{
	hypAssert(m_stackState >= AnalysisSuccessful, "");

	if (m_experimentalAnalysis)
		hypThrow(CompilerError, "IR codegen after experimental analysis is unsupported.");

	Contract& compiledContract = m_contracts.at(_contract.fullyQualifiedName());
	if (!compiledContract.yulIR.empty())
		return;

	if (!*_contract.sourceUnit().annotation().useABICoderV2)
		m_errorReporter.warning(
			2066_error,
			_contract.location(),
			"Contract requests the ABI coder v1, which is incompatible with the IR. "
			"Using ABI coder v2 instead."
		);

	std::string dependenciesSource;
	for (auto const& [dependency, referencee]: _contract.annotation().contractDependencies)
		generateIR(*dependency);

	if (!_contract.canBeDeployed())
		return;

	std::map<ContractDefinition const*, std::string_view const> otherYulSources;
	for (auto const& pair: m_contracts)
		otherYulSources.emplace(pair.second.contract, pair.second.yulIR);

	IRGenerator generator(
		m_zvmVersion,
		m_revertStrings,
		sourceIndices(),
		m_debugInfoSelection,
		this,
		m_optimiserSettings
	);
	compiledContract.yulIR = generator.run(
		_contract,
		createCBORMetadata(compiledContract, /* _forIR */ true),
		otherYulSources
	);

	yul::YulStack stack(
		m_zvmVersion,
		yul::YulStack::Language::StrictAssembly,
		m_optimiserSettings,
		m_debugInfoSelection
	);
	bool yulAnalysisSuccessful = stack.parseAndAnalyze("", compiledContract.yulIR);
	hypAssert(
		yulAnalysisSuccessful,
		compiledContract.yulIR + "\n\n"
		"Invalid IR generated:\n" +
		langutil::SourceReferenceFormatter::formatErrorInformation(stack.errors(), stack) + "\n"
	);

	compiledContract.yulIRAst = stack.astJson();
	stack.optimize();
	compiledContract.yulIROptimized = stack.print(this);
	compiledContract.yulIROptimizedAst = stack.astJson();
}

void CompilerStack::generateZVMFromIR(ContractDefinition const& _contract)
{
	hypAssert(m_stackState >= AnalysisSuccessful, "");

	if (!_contract.canBeDeployed())
		return;

	Contract& compiledContract = m_contracts.at(_contract.fullyQualifiedName());
	hypAssert(!compiledContract.yulIROptimized.empty(), "");
	if (!compiledContract.object.bytecode.empty())
		return;

	// Re-parse the Yul IR in ZVM dialect
	yul::YulStack stack(
		m_zvmVersion,
		yul::YulStack::Language::StrictAssembly,
		m_optimiserSettings,
		m_debugInfoSelection
	);
	bool analysisSuccessful = stack.parseAndAnalyze("", compiledContract.yulIROptimized);
	hypAssert(analysisSuccessful);

	//cout << yul::AsmPrinter{}(*stack.parserResult()->code) << endl;

	std::string deployedName = IRNames::deployedObject(_contract);
	hypAssert(!deployedName.empty(), "");
	tie(compiledContract.zvmAssembly, compiledContract.zvmRuntimeAssembly) = stack.assembleZVMWithDeployed(deployedName);
	assembleYul(_contract, compiledContract.zvmAssembly, compiledContract.zvmRuntimeAssembly);
}

CompilerStack::Contract const& CompilerStack::contract(std::string const& _contractName) const
{
	hypAssert(m_stackState >= AnalysisSuccessful, "");

	auto it = m_contracts.find(_contractName);
	if (it != m_contracts.end())
		return it->second;

	// To provide a measure of backward-compatibility, if a contract is not located by its
	// fully-qualified name, a lookup will be attempted purely on the contract's name to see
	// if anything will satisfy.
	if (_contractName.find(':') == std::string::npos)
	{
		for (auto const& contractEntry: m_contracts)
		{
			std::stringstream ss;
			ss.str(contractEntry.first);
			// All entries are <source>:<contract>
			std::string source;
			std::string foundName;
			getline(ss, source, ':');
			getline(ss, foundName, ':');
			if (foundName == _contractName)
				return contractEntry.second;
		}
	}

	// If we get here, both lookup methods failed.
	hypThrow(CompilerError, "Contract \"" + _contractName + "\" not found.");
}

CompilerStack::Source const& CompilerStack::source(std::string const& _sourceName) const
{
	auto it = m_sources.find(_sourceName);
	if (it == m_sources.end())
		hypThrow(CompilerError, "Given source file not found: " + _sourceName);

	return it->second;
}

std::string CompilerStack::createMetadata(Contract const& _contract, bool _forIR) const
{
	Json::Value meta{Json::objectValue};
	meta["version"] = 1;
	std::string sourceType;
	switch (m_compilationSourceType)
	{
	case CompilationSourceType::Hyperion:
		sourceType = "Hyperion";
		break;
	case CompilationSourceType::HyperionAST:
		sourceType = "HyperionAST";
		break;
	}
	meta["language"] = sourceType;
	meta["compiler"]["version"] = VersionStringStrict;

	/// All the source files (including self), which should be included in the metadata.
	std::set<std::string> referencedSources;
	referencedSources.insert(*_contract.contract->sourceUnit().annotation().path);
	for (auto const sourceUnit: _contract.contract->sourceUnit().referencedSourceUnits(true))
		referencedSources.insert(*sourceUnit->annotation().path);

	meta["sources"] = Json::objectValue;
	for (auto const& s: m_sources)
	{
		if (!referencedSources.count(s.first))
			continue;

		hypAssert(s.second.charStream, "Character stream not available");
		meta["sources"][s.first]["keccak256"] = "0x" + util::toHex(s.second.keccak256().asBytes());
		if (std::optional<std::string> licenseString = s.second.ast->licenseString())
			meta["sources"][s.first]["license"] = *licenseString;
		if (m_metadataLiteralSources)
			meta["sources"][s.first]["content"] = s.second.charStream->source();
		else
		{
			meta["sources"][s.first]["urls"] = Json::arrayValue;
			meta["sources"][s.first]["urls"].append("bzz-raw://" + util::toHex(s.second.swarmHash().asBytes()));
			meta["sources"][s.first]["urls"].append(s.second.ipfsUrl());
		}
	}

	static_assert(sizeof(m_optimiserSettings.expectedExecutionsPerDeployment) <= sizeof(Json::LargestUInt), "Invalid word size.");
	hypAssert(static_cast<Json::LargestUInt>(m_optimiserSettings.expectedExecutionsPerDeployment) < std::numeric_limits<Json::LargestUInt>::max(), "");
	meta["settings"]["optimizer"]["runs"] = Json::Value(Json::LargestUInt(m_optimiserSettings.expectedExecutionsPerDeployment));

	/// Backwards compatibility: If set to one of the default settings, do not provide details.
	OptimiserSettings settingsWithoutRuns = m_optimiserSettings;
	// reset to default
	settingsWithoutRuns.expectedExecutionsPerDeployment = OptimiserSettings::minimal().expectedExecutionsPerDeployment;
	if (settingsWithoutRuns == OptimiserSettings::minimal())
		meta["settings"]["optimizer"]["enabled"] = false;
	else if (settingsWithoutRuns == OptimiserSettings::standard())
		meta["settings"]["optimizer"]["enabled"] = true;
	else
	{
		Json::Value details{Json::objectValue};

		details["orderLiterals"] = m_optimiserSettings.runOrderLiterals;
		details["inliner"] = m_optimiserSettings.runInliner;
		details["jumpdestRemover"] = m_optimiserSettings.runJumpdestRemover;
		details["peephole"] = m_optimiserSettings.runPeephole;
		details["deduplicate"] = m_optimiserSettings.runDeduplicate;
		details["cse"] = m_optimiserSettings.runCSE;
		details["constantOptimizer"] = m_optimiserSettings.runConstantOptimiser;
		details["simpleCounterForLoopUncheckedIncrement"] = m_optimiserSettings.simpleCounterForLoopUncheckedIncrement;
		details["yul"] = m_optimiserSettings.runYulOptimiser;
		if (m_optimiserSettings.runYulOptimiser)
		{
			details["yulDetails"] = Json::objectValue;
			details["yulDetails"]["stackAllocation"] = m_optimiserSettings.optimizeStackAllocation;
			details["yulDetails"]["optimizerSteps"] = m_optimiserSettings.yulOptimiserSteps + ":" + m_optimiserSettings.yulOptimiserCleanupSteps;
		}
		else if (
			OptimiserSuite::isEmptyOptimizerSequence(m_optimiserSettings.yulOptimiserSteps) &&
			OptimiserSuite::isEmptyOptimizerSequence(m_optimiserSettings.yulOptimiserCleanupSteps)
		)
		{
			hypAssert(m_optimiserSettings.optimizeStackAllocation == false);
			details["yulDetails"] = Json::objectValue;
			details["yulDetails"]["optimizerSteps"] = ":";
		}
		else
		{
			hypAssert(m_optimiserSettings.optimizeStackAllocation == false);
			hypAssert(m_optimiserSettings.yulOptimiserSteps == OptimiserSettings::DefaultYulOptimiserSteps);
			hypAssert(m_optimiserSettings.yulOptimiserCleanupSteps == OptimiserSettings::DefaultYulOptimiserCleanupSteps);
		}

		meta["settings"]["optimizer"]["details"] = std::move(details);
	}

	if (m_revertStrings != RevertStrings::Default)
		meta["settings"]["debug"]["revertStrings"] = revertStringsToString(m_revertStrings);

	if (m_metadataFormat == MetadataFormat::NoMetadata)
		meta["settings"]["metadata"]["appendCBOR"] = false;

	if (m_metadataLiteralSources)
		meta["settings"]["metadata"]["useLiteralContent"] = true;

	static std::vector<std::string> hashes{"ipfs", "bzzr1", "none"};
	meta["settings"]["metadata"]["bytecodeHash"] = hashes.at(unsigned(m_metadataHash));

	if (_forIR)
		meta["settings"]["viaIR"] = _forIR;
	meta["settings"]["zvmVersion"] = m_zvmVersion.name();
	meta["settings"]["compilationTarget"][_contract.contract->sourceUnitName()] =
		*_contract.contract->annotation().canonicalName;

	meta["settings"]["remappings"] = Json::arrayValue;
	std::set<std::string> remappings;
	for (auto const& r: m_importRemapper.remappings())
		remappings.insert(r.context + ":" + r.prefix + "=" + r.target);
	for (auto const& r: remappings)
		meta["settings"]["remappings"].append(r);

	meta["settings"]["libraries"] = Json::objectValue;
	for (auto const& library: m_libraries)
		meta["settings"]["libraries"][library.first] = "0x" + util::toHex(library.second.asBytes());

	meta["output"]["abi"] = contractABI(_contract);
	meta["output"]["userdoc"] = natspecUser(_contract);
	meta["output"]["devdoc"] = natspecDev(_contract);

	return util::jsonCompactPrint(meta);
}

class MetadataCBOREncoder
{
public:
	void pushBytes(std::string const& key, bytes const& value)
	{
		m_entryCount++;
		pushTextString(key);
		pushByteString(value);
	}

	void pushString(std::string const& key, std::string const& value)
	{
		m_entryCount++;
		pushTextString(key);
		pushTextString(value);
	}

	void pushBool(std::string const& key, bool value)
	{
		m_entryCount++;
		pushTextString(key);
		pushBool(value);
	}

	bytes serialise() const
	{
		size_t size = m_data.size() + 1;
		hypAssert(size <= 0xffff, "Metadata too large.");
		hypAssert(m_entryCount <= 0x1f, "Too many map entries.");

		// CBOR fixed-length map
		bytes ret{static_cast<unsigned char>(0xa0 + m_entryCount)};
		// The already encoded key-value pairs
		ret += m_data;
		// 16-bit big endian length
		ret += toCompactBigEndian(size, 2);
		return ret;
	}

private:
	void pushTextString(std::string const& key)
	{
		size_t length = key.size();
		if (length < 24)
		{
			m_data += bytes{static_cast<unsigned char>(0x60 + length)};
			m_data += key;
		}
		else if (length <= 256)
		{
			m_data += bytes{0x78, static_cast<unsigned char>(length)};
			m_data += key;
		}
		else
			hypAssert(false, "Text std::string too large.");
	}
	void pushByteString(bytes const& key)
	{
		size_t length = key.size();
		if (length < 24)
		{
			m_data += bytes{static_cast<unsigned char>(0x40 + length)};
			m_data += key;
		}
		else if (length <= 256)
		{
			m_data += bytes{0x58, static_cast<unsigned char>(length)};
			m_data += key;
		}
		else
			hypAssert(false, "Byte std::string too large.");
	}
	void pushBool(bool value)
	{
		if (value)
			m_data += bytes{0xf5};
		else
			m_data += bytes{0xf4};
	}
	unsigned m_entryCount = 0;
	bytes m_data;
};

bytes CompilerStack::createCBORMetadata(Contract const& _contract, bool _forIR) const
{
	if (m_metadataFormat == MetadataFormat::NoMetadata)
		return bytes{};

	bool const experimentalMode = !onlySafeExperimentalFeaturesActivated(
		_contract.contract->sourceUnit().annotation().experimentalFeatures
	);

	std::string meta = (_forIR == m_viaIR ? metadata(_contract) : createMetadata(_contract, _forIR));

	MetadataCBOREncoder encoder;

	if (m_metadataHash == MetadataHash::IPFS)
		encoder.pushBytes("ipfs", util::ipfsHash(meta));
	else if (m_metadataHash == MetadataHash::Bzzr1)
		encoder.pushBytes("bzzr1", util::bzzr1Hash(meta).asBytes());
	else
		hypAssert(m_metadataHash == MetadataHash::None, "Invalid metadata hash");

	if (experimentalMode)
		encoder.pushBool("experimental", true);
	if (m_metadataFormat == MetadataFormat::WithReleaseVersionTag)
		encoder.pushBytes("hypc", VersionCompactBytes);
	else
	{
		hypAssert(
			m_metadataFormat == MetadataFormat::WithPrereleaseVersionTag,
			"Invalid metadata format."
		);
		encoder.pushString("hypc", VersionStringStrict);
	}
	return encoder.serialise();
}

namespace
{

Json::Value gasToJson(GasEstimator::GasConsumption const& _gas)
{
	if (_gas.isInfinite)
		return Json::Value("infinite");
	else
		return Json::Value(util::toString(_gas.value));
}

}

Json::Value CompilerStack::gasEstimates(std::string const& _contractName) const
{
	if (m_stackState != CompilationSuccessful)
		hypThrow(CompilerError, "Compilation was not successful.");

	if (!assemblyItems(_contractName) && !runtimeAssemblyItems(_contractName))
		return Json::Value();

	using Gas = GasEstimator::GasConsumption;
	GasEstimator gasEstimator(m_zvmVersion);
	Json::Value output(Json::objectValue);

	if (zvmasm::AssemblyItems const* items = assemblyItems(_contractName))
	{
		Gas executionGas = gasEstimator.functionalEstimation(*items);
		Gas codeDepositGas{zvmasm::GasMeter::dataGas(runtimeObject(_contractName).bytecode, false)};

		Json::Value creation(Json::objectValue);
		creation["codeDepositCost"] = gasToJson(codeDepositGas);
		creation["executionCost"] = gasToJson(executionGas);
		/// TODO: implement + overload to avoid the need of +=
		executionGas += codeDepositGas;
		creation["totalCost"] = gasToJson(executionGas);
		output["creation"] = creation;
	}

	if (zvmasm::AssemblyItems const* items = runtimeAssemblyItems(_contractName))
	{
		/// External functions
		ContractDefinition const& contract = contractDefinition(_contractName);
		Json::Value externalFunctions(Json::objectValue);
		for (auto it: contract.interfaceFunctions())
		{
			std::string sig = it.second->externalSignature();
			externalFunctions[sig] = gasToJson(gasEstimator.functionalEstimation(*items, sig));
		}

		if (contract.fallbackFunction())
			/// This needs to be set to an invalid signature in order to trigger the fallback,
			/// without the shortcut (of CALLDATSIZE == 0), and therefore to receive the upper bound.
			/// An empty string ("") would work to trigger the shortcut only.
			externalFunctions[""] = gasToJson(gasEstimator.functionalEstimation(*items, "INVALID"));

		if (!externalFunctions.empty())
			output["external"] = externalFunctions;

		/// Internal functions
		Json::Value internalFunctions(Json::objectValue);
		for (auto const& it: contract.definedFunctions())
		{
			/// Exclude externally visible functions, constructor, fallback and receive ether function
			if (it->isPartOfExternalInterface() || !it->isOrdinary())
				continue;

			size_t entry = functionEntryPoint(_contractName, *it);
			GasEstimator::GasConsumption gas = GasEstimator::GasConsumption::infinite();
			if (entry > 0)
				gas = gasEstimator.functionalEstimation(*items, entry, *it);

			/// TODO: This could move into a method shared with externalSignature()
			FunctionType type(*it);
			std::string sig = it->name() + "(";
			auto paramTypes = type.parameterTypes();
			for (auto it = paramTypes.begin(); it != paramTypes.end(); ++it)
				sig += (*it)->toString() + (it + 1 == paramTypes.end() ? "" : ",");
			sig += ")";

			internalFunctions[sig] = gasToJson(gas);
		}

		if (!internalFunctions.empty())
			output["internal"] = internalFunctions;
	}

	return output;
}
