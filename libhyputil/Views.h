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

#include <range/v3/view/transform.hpp>

#include <liblangutil/Exceptions.h>


namespace hyperion::util::views
{

static constexpr auto dereference = ranges::views::transform([](auto&& x) -> decltype(auto) { return *x; });

static constexpr auto dereferenceChecked = ranges::views::transform(
	[](auto&& x) -> decltype(auto)
	{
		hypAssert(x, "");
		return *x;
	}
);

}
