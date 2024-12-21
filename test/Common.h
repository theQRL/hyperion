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

#include <libhyputil/Exceptions.h>
#include <liblangutil/ZVMVersion.h>
#include <liblangutil/Exceptions.h>
#include <libhyputil/Numeric.h>

#include <test/zvmc/zvmc.h>

#include <boost/filesystem/path.hpp>
#include <boost/program_options.hpp>

namespace hyperion::test
{

#ifdef _WIN32
static constexpr auto zvmoneFilename = "zvmone.dll";
static constexpr auto zvmoneDownloadLink = "https://github.com/theQRL/zvmone/releases/download/v0.1.0/zvmone-0.1.0-windows-amd64.zip";
#elif defined(__APPLE__)
static constexpr auto zvmoneFilename = "libzvmone.dylib";
static constexpr auto zvmoneDownloadLink = "https://github.com/theQRL/zvmone/releases/download/v0.1.0/zvmone-0.1.0-darwin-x86_64.tar.gz";
#else
static constexpr auto zvmoneFilename = "libzvmone.so";
static constexpr auto zvmoneDownloadLink = "https://github.com/theQRL/zvmone/releases/download/v0.1.0/zvmone-0.1.0-linux-x86_64.tar.gz";
#endif

struct ConfigException: public util::Exception {};

struct CommonOptions
{
	/// Noncopyable.
	CommonOptions(CommonOptions const&) = delete;
	CommonOptions& operator=(CommonOptions const&) = delete;

	std::vector<boost::filesystem::path> vmPaths;
	boost::filesystem::path testPath;
	bool optimize = false;
	bool enforceGasTest = false;
	u256 enforceGasTestMinValue = 100000;
	bool disableSemanticTests = false;
	bool disableSMT = false;
	bool useABIEncoderV1 = false;
	bool showMessages = false;
	bool showMetadata = false;
	size_t batches = 1;
	size_t selectedBatch = 0;

	langutil::ZVMVersion zvmVersion() const;

	virtual void addOptions();
	// @returns true if the program should continue, false if it should exit immediately without
	// reporting an error.
	// Throws ConfigException or std::runtime_error if parsing fails.
	virtual bool parse(int argc, char const* const* argv);
	// Throws a ConfigException on error
	virtual void validate() const;

	/// @returns string with a key=value list of the options separated by comma
	/// Ex.: "zvmVersion=shanghai, optimize=true, useABIEncoderV1=false"
	virtual std::string toString(std::vector<std::string> const& _selectedOptions) const;
	/// Helper to print the value of settings used
	virtual void printSelectedOptions(std::ostream& _stream, std::string const& _linePrefix, std::vector<std::string> const& _selectedOptions) const;

	static CommonOptions const& get();
	static void setSingleton(std::unique_ptr<CommonOptions const>&& _instance);

	CommonOptions(std::string caption = "");
	virtual ~CommonOptions() {}

protected:
	boost::program_options::options_description options;

private:
	std::string zvmVersionString;
	static std::unique_ptr<CommonOptions const> m_singleton;
};

/// @return true if it is ok to treat the file located under the specified path as a semantic test.
/// I.e. if the test is located in the semantic test directory and is not excluded due to being a part of external sources.
/// Note: @p _testPath can be relative but must include at least the `/test/libhyperion/semanticTests/` part
bool isValidSemanticTestPath(boost::filesystem::path const& _testPath);

bool loadVMs(CommonOptions const& _options);

/**
 * Component to help with splitting up all tests into batches.
 */
class Batcher
{
public:
	Batcher(size_t _offset, size_t _batches):
		m_offset(_offset),
		m_batches(_batches)
	{
		hypAssert(m_batches > 0 && m_offset < m_batches);
	}
	Batcher(Batcher const&) = delete;
	Batcher& operator=(Batcher const&) = delete;

	bool checkAndAdvance() { return (m_counter++) % m_batches == m_offset; }

private:
	size_t const m_offset;
	size_t const m_batches;
	size_t m_counter = 0;
};

}
