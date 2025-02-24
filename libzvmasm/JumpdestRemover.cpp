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
 * @author Alex Beregszaszi
 * Removes unused JUMPDESTs.
 */

#include <libzvmasm/JumpdestRemover.h>

#include <libzvmasm/AssemblyItem.h>

#include <limits>

using namespace hyperion;
using namespace hyperion::util;
using namespace hyperion::zvmasm;

bool JumpdestRemover::optimise(std::set<size_t> const& _tagsReferencedFromOutside)
{
	std::set<size_t> references{referencedTags(m_items, std::numeric_limits<size_t>::max())};
	references.insert(_tagsReferencedFromOutside.begin(), _tagsReferencedFromOutside.end());

	size_t initialSize = m_items.size();
	/// Remove tags which are never referenced.
	auto pend = remove_if(
		m_items.begin(),
		m_items.end(),
		[&](AssemblyItem const& _item)
		{
			if (_item.type() != Tag)
				return false;
			auto asmIdAndTag = _item.splitForeignPushTag();
			assertThrow(asmIdAndTag.first == std::numeric_limits<size_t>::max(), OptimizerException, "Sub-assembly tag used as label.");
			size_t tag = asmIdAndTag.second;
			return !references.count(tag);
		}
	);
	m_items.erase(pend, m_items.end());
	return m_items.size() != initialSize;
}

std::set<size_t> JumpdestRemover::referencedTags(AssemblyItems const& _items, size_t _subId)
{
	std::set<size_t> ret;
	for (auto const& item: _items)
		if (item.type() == PushTag)
		{
			auto subAndTag = item.splitForeignPushTag();
			if (subAndTag.first == _subId)
				ret.insert(subAndTag.second);
		}
	return ret;
}
