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

#include <liblangutil/DebugInfoSelection.h>

#include <liblangutil/Exceptions.h>

#include <libhyputil/StringUtils.h>

#include <boost/algorithm/string/trim.hpp>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/map.hpp>
#include <range/v3/view/split.hpp>

#include <vector>

using namespace hyperion;
using namespace hyperion::langutil;
using namespace hyperion::util;

DebugInfoSelection const DebugInfoSelection::All(bool _value) noexcept
{
	DebugInfoSelection result;
	for (bool DebugInfoSelection::* member: componentMap() | ranges::views::values)
		result.*member = _value;
	return result;
}

DebugInfoSelection const DebugInfoSelection::Only(bool DebugInfoSelection::* _member) noexcept
{
	DebugInfoSelection result{};
	result.*_member = true;
	return result;
}

std::optional<DebugInfoSelection> DebugInfoSelection::fromString(std::string_view _input)
{
	// TODO: Make more stuff constexpr and make it a static_assert().
	hypAssert(componentMap().count("all") == 0, "");
	hypAssert(componentMap().count("none") == 0, "");

	if (_input == "all")
		return All();
	if (_input == "none")
		return None();

	return fromComponents(_input | ranges::views::split(',') | ranges::to<std::vector<std::string>>);
}

std::optional<DebugInfoSelection> DebugInfoSelection::fromComponents(
	std::vector<std::string> const& _componentNames,
	bool _acceptWildcards
)
{
	hypAssert(componentMap().count("*") == 0, "");

	DebugInfoSelection selection;
	for (auto const& component: _componentNames)
	{
		if (component == "*")
			return (_acceptWildcards ? std::make_optional(DebugInfoSelection::All()) : std::nullopt);

		if (!selection.enable(component))
			return std::nullopt;
	}

	return selection;
}

bool DebugInfoSelection::enable(std::string _component)
{
	auto memberIt = componentMap().find(boost::trim_copy(_component));
	if (memberIt == componentMap().end())
		return false;

	this->*(memberIt->second) = true;
	return true;
}

bool DebugInfoSelection::any() const noexcept
{
	for (bool DebugInfoSelection::* member: componentMap() | ranges::views::values)
		if (this->*member)
			return true;

	return false;
}

bool DebugInfoSelection::all() const noexcept
{
	for (bool DebugInfoSelection::* member: componentMap() | ranges::views::values)
		if (!(this->*member))
			return false;

	return true;
}

DebugInfoSelection& DebugInfoSelection::operator&=(DebugInfoSelection const& _other)
{
	for (bool DebugInfoSelection::* member: componentMap() | ranges::views::values)
		this->*member &= _other.*member;
	return *this;
}

DebugInfoSelection& DebugInfoSelection::operator|=(DebugInfoSelection const& _other)
{
	for (bool DebugInfoSelection::* member: componentMap() | ranges::views::values)
		this->*member |= _other.*member;
	return *this;
}

DebugInfoSelection DebugInfoSelection::operator&(DebugInfoSelection _other) const noexcept
{
	_other &= *this;
	return _other;
}

DebugInfoSelection DebugInfoSelection::operator|(DebugInfoSelection _other) const noexcept
{
	_other |= *this;
	return _other;
}

bool DebugInfoSelection::operator==(DebugInfoSelection const& _other) const noexcept
{
	for (bool DebugInfoSelection::* member: componentMap() | ranges::views::values)
		if (this->*member != _other.*member)
			return false;
	return true;
}

std::ostream& langutil::operator<<(std::ostream& _stream, DebugInfoSelection const& _selection)
{
	std::vector<std::string> selectedComponentNames;
	for (auto const& [name, member]: _selection.componentMap())
		if (_selection.*member)
			selectedComponentNames.push_back(name);

	return _stream << joinHumanReadable(selectedComponentNames, ",");
}
