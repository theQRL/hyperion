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

#include <libhyperion/formal/Invariants.h>

#include <libhyperion/formal/ExpressionFormatter.h>
#include <libhyperion/formal/SMTEncoder.h>

#include <libhyputil/Algorithms.h>

#include <boost/algorithm/string.hpp>

using boost::algorithm::starts_with;
using namespace hyperion;
using namespace hyperion::smtutil;
using namespace hyperion::frontend::smt;

namespace hyperion::frontend::smt
{

std::map<Predicate const*, std::set<std::string>> collectInvariants(
	smtutil::Expression const& _proof,
	std::set<Predicate const*> const& _predicates,
	ModelCheckerInvariants const& _invariantsSetting
)
{
	std::set<std::string> targets;
	if (_invariantsSetting.has(InvariantType::Contract))
		targets.insert("interface_");
	if (_invariantsSetting.has(InvariantType::Reentrancy))
		targets.insert("nondet_interface_");

	std::map<std::string, std::pair<smtutil::Expression, smtutil::Expression>> equalities;
	// Collect equalities where one of the sides is a predicate we're interested in.
	util::BreadthFirstSearch<smtutil::Expression const*>{{&_proof}}.run([&](auto&& _expr, auto&& _addChild) {
		if (_expr->name == "=")
			for (auto const& t: targets)
			{
				auto arg0 = _expr->arguments.at(0);
				auto arg1 = _expr->arguments.at(1);
				if (starts_with(arg0.name, t))
					equalities.insert({arg0.name, {arg0, std::move(arg1)}});
				else if (starts_with(arg1.name, t))
					equalities.insert({arg1.name, {arg1, std::move(arg0)}});
			}
		for (auto const& arg: _expr->arguments)
			_addChild(&arg);
	});

	std::map<Predicate const*, std::set<std::string>> invariants;
	for (auto pred: _predicates)
	{
		auto predName = pred->functor().name;
		if (!equalities.count(predName))
			continue;

		hypAssert(pred->contextContract(), "");

		auto const& [predExpr, invExpr] = equalities.at(predName);

		static std::set<std::string> const ignore{"true", "false"};
		auto r = substitute(invExpr, pred->expressionSubstitution(predExpr));
		// No point in reporting true/false as invariants.
		if (!ignore.count(r.name))
			invariants[pred].insert(toHyperionStr(r));
	}
	return invariants;
}

}
