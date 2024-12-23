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
/** @file IhypTestOptions.h
 */

#pragma once

#include <liblangutil/ZVMVersion.h>

#include <test/Common.h>

namespace hyperion::test
{

struct IhypTestOptions: CommonOptions
{
	bool showHelp = false;
	bool noColor = false;
	bool acceptUpdates = false;
	std::string testFilter = std::string{};
	std::string editor = std::string{};

	explicit IhypTestOptions();
	void addOptions() override;
	bool parse(int _argc, char const* const* _argv) override;
	void validate() const override;
};

}
