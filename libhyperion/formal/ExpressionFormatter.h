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

/**
 * Formats SMT expressions into Hyperion-like strings.
 */

#include <libhyperion/formal/Predicate.h>

#include <map>
#include <string>

namespace hyperion::frontend::smt
{

/// @returns another smtutil::Expressions where every term in _from
/// may be replaced if it is in the substitution map _subst.
smtutil::Expression substitute(smtutil::Expression _from, std::map<std::string, std::string> const& _subst);

/// @returns a Hyperion-like expression string built from _expr.
/// This is done at best-effort and is not guaranteed to always create a perfect Hyperion expression string.
std::string toHyperionStr(smtutil::Expression const& _expr);

}
