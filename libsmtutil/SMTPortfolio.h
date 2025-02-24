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


#include <libsmtutil/SolverInterface.h>
#include <libhyperion/interface/ReadFile.h>
#include <libhyputil/FixedHash.h>

#include <map>
#include <vector>

namespace hyperion::smtutil
{

/**
 * The SMTPortfolio wraps all available solvers within a single interface,
 * propagating the functionalities to all solvers.
 * It also checks whether different solvers give conflicting answers
 * to SMT queries.
 */
class SMTPortfolio: public SolverInterface
{
public:
	/// Noncopyable.
	SMTPortfolio(SMTPortfolio const&) = delete;
	SMTPortfolio& operator=(SMTPortfolio const&) = delete;

	SMTPortfolio(
		std::map<util::h256, std::string> _smtlib2Responses = {},
		frontend::ReadCallback::Callback _smtCallback = {},
		SMTSolverChoice _enabledSolvers = SMTSolverChoice::All(),
		std::optional<unsigned> _queryTimeout = {},
		bool _printQuery = false
	);

	void reset() override;

	void push() override;
	void pop() override;

	void declareVariable(std::string const&, SortPointer const&) override;

	void addAssertion(Expression const& _expr) override;

	std::pair<CheckResult, std::vector<std::string>> check(std::vector<Expression> const& _expressionsToEvaluate) override;

	std::vector<std::string> unhandledQueries() override;
	size_t solvers() override { return m_solvers.size(); }

	std::string dumpQuery(std::vector<Expression> const& _expressionsToEvaluate);

private:
	static bool solverAnswered(CheckResult result);

	std::vector<std::unique_ptr<SolverInterface>> m_solvers;

	std::vector<Expression> m_assertions;
};

}
