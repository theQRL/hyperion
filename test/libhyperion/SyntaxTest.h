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

#pragma once

#include <test/libhyperion/AnalysisFramework.h>
#include <test/TestCase.h>
#include <test/CommonSyntaxTest.h>
#include <liblangutil/Exceptions.h>
#include <libhyputil/AnsiColorized.h>

#include <iosfwd>
#include <string>
#include <vector>
#include <utility>

namespace hyperion::frontend::test
{

using hyperion::test::SyntaxTestError;

class SyntaxTest: public AnalysisFramework, public hyperion::test::CommonSyntaxTest
{
public:
	static std::unique_ptr<TestCase> create(Config const& _config)
	{
		return std::make_unique<SyntaxTest>(_config.filename, _config.zvmVersion);
	}
	SyntaxTest(
		std::string const& _filename,
		langutil::ZVMVersion _zvmVersion,
		langutil::Error::Severity _minSeverity = langutil::Error::Severity::Info
	);

protected:
	void setupCompiler(CompilerStack& _compiler) override;
	void parseAndAnalyze() override;
	virtual void filterObtainedErrors();

	bool m_optimiseYul{};
	std::string m_compileViaYul{};
	langutil::Error::Severity m_minSeverity{};
};

}
