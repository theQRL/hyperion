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

#include <libhyperion/interface/ReadFile.h>

#include <boost/filesystem.hpp>

namespace hyperion::frontend
{

/// SMTSolverCommand wraps an SMT solver called via its binary in the OS.
class SMTSolverCommand
{
public:
	SMTSolverCommand(std::string _solverCmd);

	/// Calls an SMT solver with the given query.
	frontend::ReadCallback::Result solve(std::string const& _kind, std::string const& _query);

	frontend::ReadCallback::Callback solver()
	{
		return [this](std::string const& _kind, std::string const& _query) { return solve(_kind, _query); };
	}

private:
	/// The name of the solver's binary.
	std::string const m_solverCmd;
};

}
