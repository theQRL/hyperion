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
 * @author Christian <c@ethdev.com>
 * @date 2015
 * Versioning.
 */

#include <libhyperion/interface/Version.h>

#include <hyperion/BuildInfo.h>

char const* hyperion::frontend::VersionNumber = ZOND_PROJECT_VERSION;

std::string const hyperion::frontend::VersionString =
	std::string(hyperion::frontend::VersionNumber) +
	(std::string(HYP_VERSION_PRERELEASE).empty() ? "" : "-" + std::string(HYP_VERSION_PRERELEASE)) +
	(std::string(HYP_VERSION_BUILDINFO).empty() ? "" : "+" + std::string(HYP_VERSION_BUILDINFO));

std::string const hyperion::frontend::VersionStringStrict =
	std::string(hyperion::frontend::VersionNumber) +
	(std::string(HYP_VERSION_PRERELEASE).empty() ? "" : "-" + std::string(HYP_VERSION_PRERELEASE)) +
	(std::string(HYP_VERSION_COMMIT).empty() ? "" : "+" + std::string(HYP_VERSION_COMMIT));

hyperion::bytes const hyperion::frontend::VersionCompactBytes = {
	ZOND_PROJECT_VERSION_MAJOR,
	ZOND_PROJECT_VERSION_MINOR,
	ZOND_PROJECT_VERSION_PATCH
};

bool const hyperion::frontend::VersionIsRelease = std::string(HYP_VERSION_PRERELEASE).empty();
