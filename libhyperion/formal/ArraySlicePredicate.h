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

#include <libhyperion/formal/EncodingContext.h>
#include <libhyperion/formal/Predicate.h>
#include <libhyperion/formal/SymbolicVariables.h>

#include <libsmtutil/Sorts.h>

#include <vector>

namespace hyperion::frontend
{

/**
 * Contains the set of rules to compute an array slice.
 * Rules:
 * 1. end > start => ArraySliceHeader(a, b, start, end, 0)
 * 2. ArraySliceHeader(a, b, start, end, i) && i >= (end - start) => ArraySlice(a, b, start, end)
 * 3. ArraySliceHeader(a, b, start, end, i) && i >= 0 && i < (end - start) => ArraySliceLoop(a, b, start, end, i)
 * 4. ArraySliceLoop(a, b, start, end, i) && b[i] = a[start + i] => ArraySliceHeader(a, b, start, end, i + 1)
 *
 * The rule to be used by CHC is ArraySlice(a, b, start, end).
 */

struct ArraySlicePredicate
{
	/// Contains the predicates and rules created to compute
	/// array slices for a given sort.
	struct SliceData
	{
		std::vector<Predicate const*> predicates;
		std::vector<smtutil::Expression> rules;
	};

	/// @returns a flag representing whether the array slice predicates had already been created before for this sort,
	/// and the corresponding slice data.
	static std::pair<bool, SliceData const&> create(smtutil::SortPointer _sort, smt::EncodingContext& _context);

	static void reset() { m_slicePredicates.clear(); }

private:
	/// Maps a unique sort name to its slice data.
	static std::map<std::string, SliceData> m_slicePredicates;
};

}
