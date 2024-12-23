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

#include <test/libhyperion/SyntaxTest.h>

#include <test/libhyperion/util/Common.h>
#include <test/Common.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/throw_exception.hpp>
#include <fstream>
#include <memory>
#include <stdexcept>

using namespace hyperion;
using namespace hyperion::util;
using namespace hyperion::util::formatting;
using namespace hyperion::langutil;
using namespace hyperion::frontend;
using namespace hyperion::frontend::test;
using namespace boost::unit_test;
namespace fs = boost::filesystem;

SyntaxTest::SyntaxTest(
	std::string const& _filename,
	langutil::ZVMVersion _zvmVersion,
	Error::Severity _minSeverity
):
	CommonSyntaxTest(_filename, _zvmVersion),
	m_minSeverity(_minSeverity)
{
	static std::set<std::string> const compileViaYulAllowedValues{"true", "false"};

	m_compileViaYul = m_reader.stringSetting("compileViaYul", "false");
	if (!util::contains(compileViaYulAllowedValues, m_compileViaYul))
		BOOST_THROW_EXCEPTION(std::runtime_error("Invalid compileViaYul value: " + m_compileViaYul + "."));
	m_optimiseYul = m_reader.boolSetting("optimize-yul", true);
}

void SyntaxTest::setupCompiler(CompilerStack& _compiler)
{
	AnalysisFramework::setupCompiler(_compiler);

	_compiler.setZVMVersion(m_zvmVersion);
	_compiler.setOptimiserSettings(
		m_optimiseYul ?
		OptimiserSettings::full() :
		OptimiserSettings::minimal()
	);
	_compiler.setViaIR(m_compileViaYul == "true");
	_compiler.setMetadataFormat(CompilerStack::MetadataFormat::NoMetadata);
	_compiler.setMetadataHash(CompilerStack::MetadataHash::None);
}

void SyntaxTest::parseAndAnalyze()
{
	try
	{
		runFramework(withPreamble(m_sources.sources), PipelineStage::Compilation);
		if (!pipelineSuccessful() && stageSuccessful(PipelineStage::Analysis))
		{
			ErrorList const& errors = compiler().errors();
			auto codeGeneretionErrorCount = count_if(errors.cbegin(), errors.cend(), [](auto const& error) {
				return error->type() == Error::Type::CodeGenerationError;
			});
			auto errorCount = count_if(errors.cbegin(), errors.cend(), [](auto const& error) {
				return Error::isError(error->type());
			});
			// failing compilation after successful analysis is a rare case,
			// it assumes that errors contain exactly one error, and the error is of type Error::Type::CodeGenerationError
			if (codeGeneretionErrorCount != 1 || errorCount != 1)
				BOOST_THROW_EXCEPTION(std::runtime_error("Compilation failed even though analysis was successful."));
		}
	}
	catch (UnimplementedFeatureError const& _e)
	{
		m_errorList.emplace_back(SyntaxTestError{
			Error::Type::UnimplementedFeatureError,
			std::nullopt,
			errorMessage(_e),
			"",
			-1,
			-1
		});
	}

	filterObtainedErrors();
}

void SyntaxTest::filterObtainedErrors()
{
	for (auto const& currentError: filteredErrors())
	{
		if (currentError->severity() < m_minSeverity)
			continue;

		int locationStart = -1;
		int locationEnd = -1;
		std::string sourceName;
		if (SourceLocation const* location = currentError->sourceLocation())
		{
			locationStart = location->start;
			locationEnd = location->end;
			hypAssert(location->sourceName, "");
			sourceName = *location->sourceName;
			if(m_sources.sources.count(sourceName) == 1)
			{
				int preambleSize =
						static_cast<int>(compiler().charStream(sourceName).size()) -
						static_cast<int>(m_sources.sources[sourceName].size());
				hypAssert(preambleSize >= 0, "");

				// ignore the version & license pragma inserted by the testing tool when calculating locations.
				if (location->start != -1)
				{
					hypAssert(location->start >= preambleSize, "");
					locationStart = location->start - preambleSize;
				}
				if (location->end != -1)
				{
					hypAssert(location->end >= preambleSize, "");
					locationEnd = location->end - preambleSize;
				}
			}
		}
		m_errorList.emplace_back(SyntaxTestError{
			currentError->type(),
			currentError->errorId(),
			errorMessage(*currentError),
			sourceName,
			locationStart,
			locationEnd
		});
	}
}
