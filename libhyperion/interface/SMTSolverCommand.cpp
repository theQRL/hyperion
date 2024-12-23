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
#include <libhyperion/interface/SMTSolverCommand.h>

#include <liblangutil/Exceptions.h>

#include <libhyputil/CommonIO.h>
#include <libhyputil/Exceptions.h>
#include <libhyputil/Keccak256.h>
#include <libhyputil/TemporaryDirectory.h>

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/process.hpp>

using hyperion::langutil::InternalCompilerError;
using hyperion::util::errinfo_comment;


namespace hyperion::frontend
{

SMTSolverCommand::SMTSolverCommand(std::string _solverCmd) : m_solverCmd(_solverCmd) {}

ReadCallback::Result SMTSolverCommand::solve(std::string const& _kind, std::string const& _query)
{
	try
	{
		if (_kind != ReadCallback::kindString(ReadCallback::Kind::SMTQuery))
			hypAssert(false, "SMTQuery callback used as callback kind " + _kind);

		auto tempDir = hyperion::util::TemporaryDirectory("smt");
		util::h256 queryHash = util::keccak256(_query);
		auto queryFileName = tempDir.path() / ("query_" + queryHash.hex() + ".smt2");

		auto queryFile = boost::filesystem::ofstream(queryFileName);
		queryFile << _query;

		auto eldBin = boost::process::search_path(m_solverCmd);

		if (eldBin.empty())
			return ReadCallback::Result{false, m_solverCmd + " binary not found."};

		boost::process::ipstream pipe;
		boost::process::child eld(
			eldBin,
			queryFileName,
			boost::process::std_out > pipe
		);

		std::vector<std::string> data;
		std::string line;
		while (eld.running() && std::getline(pipe, line))
			if (!line.empty())
				data.push_back(line);

		eld.wait();

		return ReadCallback::Result{true, boost::join(data, "\n")};
	}
	catch (...)
	{
		return ReadCallback::Result{false, "Unknown exception in SMTQuery callback: " + boost::current_exception_diagnostic_information()};
	}
}

}
