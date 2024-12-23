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
 * Optimisation stage that replaces constants by expressions that compute them.
 */

#pragma once

#include <libyul/optimiser/ASTWalker.h>
#include <libyul/YulString.h>
#include <libyul/Dialect.h>
#include <libyul/backends/zvm/ZVMDialect.h>
#include <libyul/ASTForward.h>

#include <liblangutil/SourceLocation.h>

#include <libhyputil/Common.h>

#include <tuple>
#include <map>
#include <memory>

namespace hyperion::yul
{
struct Dialect;
class GasMeter;

/**
 * Optimisation stage that replaces constants by expressions that compute them.
 *
 * Prerequisite: None
 */
class ConstantOptimiser: public ASTModifier
{
public:
	ConstantOptimiser(ZVMDialect const& _dialect, GasMeter const& _meter):
		m_dialect(_dialect),
		m_meter(_meter)
	{}

	void visit(Expression& _e) override;

	struct Representation
	{
		std::unique_ptr<Expression> expression;
		bigint cost;
	};

private:
	ZVMDialect const& m_dialect;
	GasMeter const& m_meter;
	std::map<u256, Representation> m_cache;
};

class RepresentationFinder
{
public:
	using Representation = ConstantOptimiser::Representation;
	RepresentationFinder(
		ZVMDialect const& _dialect,
		GasMeter const& _meter,
		std::shared_ptr<DebugData const> _debugData,
		std::map<u256, Representation>& _cache
	):
		m_dialect(_dialect),
		m_meter(_meter),
		m_debugData(std::move(_debugData)),
		m_cache(_cache)
	{}

	/// @returns a cheaper representation for the number than its representation
	/// as a literal or nullptr otherwise.
	Expression const* tryFindRepresentation(u256 const& _value);

private:
	/// Recursively try to find the cheapest representation of the given number,
	/// literal if necessary.
	Representation const& findRepresentation(u256 const& _value);

	Representation represent(u256 const& _value) const;
	Representation represent(YulString _instruction, Representation const& _arg) const;
	Representation represent(YulString _instruction, Representation const& _arg1, Representation const& _arg2) const;

	Representation min(Representation _a, Representation _b);

	ZVMDialect const& m_dialect;
	GasMeter const& m_meter;
	std::shared_ptr<DebugData const> m_debugData;
	/// Counter for the complexity of optimization, will stop when it reaches zero.
	size_t m_maxSteps = 10000;
	std::map<u256, Representation>& m_cache;
};

}
