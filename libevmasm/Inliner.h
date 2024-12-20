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
 * @file Inliner.h
 * Inlines small code snippets by replacing JUMP with a copy of the code jumped to.
 */
#pragma once

#include <libhyputil/Common.h>
#include <libzvmasm/Assembly.h>
#include <libzvmasm/AssemblyItem.h>
#include <liblangutil/ZVMVersion.h>

#include <range/v3/view/span.hpp>
#include <map>
#include <set>
#include <vector>

namespace hyperion::zvmasm
{

class Inliner
{
public:
	explicit Inliner(
		AssemblyItems& _items,
		std::set<size_t> const& _tagsReferencedFromOutside,
		size_t _runs,
		bool _isCreation,
		langutil::ZVMVersion _zvmVersion
	):
	m_items(_items),
	m_tagsReferencedFromOutside(_tagsReferencedFromOutside),
	m_runs(_runs),
	m_isCreation(_isCreation),
	m_zvmVersion(_zvmVersion)
	{
	}
	virtual ~Inliner() = default;

	void optimise();

private:
	struct InlinableBlock
	{
		ranges::span<AssemblyItem const> items;
		uint64_t pushTagCount = 0;
	};

	/// @returns the exit item for the block to be inlined, if a particular jump to it should be inlined, otherwise nullopt.
	std::optional<AssemblyItem> shouldInline(size_t _tag, AssemblyItem const& _jump, InlinableBlock const& _block) const;
	/// @returns true, if the full function at tag @a _tag with body @a _block that is referenced @a _pushTagCount times
	/// should be inlined, false otherwise. @a _block should start at the first instruction after the function entry tag
	/// up to and including the return jump.
	bool shouldInlineFullFunctionBody(size_t _tag, ranges::span<AssemblyItem const> _block, uint64_t _pushTagCount) const;
	/// @returns true, if the @a _items at @a _tag are a potential candidate for inlining.
	bool isInlineCandidate(size_t _tag, ranges::span<AssemblyItem const> _items) const;
	/// @returns a map from tags that can potentially be inlined to the inlinable item range behind that tag and the
	/// number of times the tag in question was referenced.
	std::map<size_t, InlinableBlock> determineInlinableBlocks(AssemblyItems const& _items) const;

	AssemblyItems& m_items;
	std::set<size_t> const& m_tagsReferencedFromOutside;
	size_t const m_runs = Assembly::OptimiserSettings{}.expectedExecutionsPerDeployment;
	bool const m_isCreation = false;
	langutil::ZVMVersion const m_zvmVersion;
};

}
