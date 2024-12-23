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

/// Unit tests for hypc/CommandLineInterface.h

#include <hypc/CommandLineInterface.h>
#include <hypc/Exceptions.h>

#include <test/hypc/Common.h>

#include <test/Common.h>
#include <test/libhyperion/util/Common.h>
#include <test/libhyperion/util/HyptestErrors.h>
#include <liblangutil/SemVerHandler.h>
#include <test/FilesystemUtils.h>

#include <libhyputil/JSON.h>
#include <libhyputil/TemporaryDirectory.h>

#include <boost/algorithm/string.hpp>

#include <range/v3/view/transform.hpp>

#include <map>
#include <ostream>
#include <set>
#include <string>
#include <vector>

using namespace std;
using namespace hyperion::frontend;
using namespace hyperion::test;
using namespace hyperion::util;
using namespace hyperion::langutil;

using PathSet = set<boost::filesystem::path>;

#define TEST_CASE_NAME (boost::unit_test::framework::current_test_case().p_name)

namespace
{

ostream& operator<<(ostream& _out, vector<ImportRemapper::Remapping> const& _remappings)
{
	static auto remappingToString = [](auto const& _remapping)
	{
		return _remapping.context + ":" + _remapping.prefix + "=" + _remapping.target;
	};

	_out << "[" << joinHumanReadable(_remappings | ranges::views::transform(remappingToString)) << "]";
	return _out;
}

ostream& operator<<(ostream& _out, map<string, string> const& _map)
{
	_out << "{" << endl;
	for (auto const& [key, value]: _map)
		_out << "" << key << ": " << value << "," << endl;
	_out << "}";

	return _out;
}

ostream& operator<<(ostream& _out, PathSet const& _paths)
{
	static auto pathString = [](auto const& _path) { return _path.string(); };

	_out << "{" << joinHumanReadable(_paths | ranges::views::transform(pathString)) << "}";
	return _out;
}

} // namespace

namespace boost::test_tools::tt_detail
{

// Boost won't find the << operator unless we put it in the std namespace which is illegal.
// The recommended solution is to overload print_log_value<> struct and make it use our operator.

template<>
struct print_log_value<vector<ImportRemapper::Remapping>>
{
	void operator()(std::ostream& _out, vector<ImportRemapper::Remapping> const& _value) { ::operator<<(_out, _value); }
};

template<>
struct print_log_value<map<string, string>>
{
	void operator()(std::ostream& _out, map<string, string> const& _value) { ::operator<<(_out, _value); }
};

template<>
struct print_log_value<PathSet>
{
	void operator()(std::ostream& _out, PathSet const& _value) { ::operator<<(_out, _value); }
};

} // namespace boost::test_tools::tt_detail

namespace hyperion::frontend::test
{

BOOST_AUTO_TEST_SUITE(CommandLineInterfaceTest)

BOOST_AUTO_TEST_CASE(help)
{
	OptionsReaderAndMessages result = runCLI({"hypc", "--help"}, "");

	BOOST_TEST(result.success);
	BOOST_TEST(boost::starts_with(result.stdoutContent, "hypc, the Hyperion commandline compiler."));
	BOOST_TEST(result.stderrContent == "");
	BOOST_TEST(result.options.input.mode == InputMode::Help);
}

BOOST_AUTO_TEST_CASE(license)
{
	OptionsReaderAndMessages result = runCLI({"hypc", "--license"}, "");

	BOOST_TEST(result.success);
	BOOST_TEST(boost::starts_with(result.stdoutContent, "Most of the code is licensed under GPLv3"));
	BOOST_TEST(result.stderrContent == "");
	BOOST_TEST(result.options.input.mode == InputMode::License);
}

BOOST_AUTO_TEST_CASE(version)
{
	OptionsReaderAndMessages result = runCLI({"hypc", "--version"}, "");

	BOOST_TEST(result.success);
	BOOST_TEST(boost::ends_with(result.stdoutContent, "Version: " + hyperion::frontend::VersionString + "\n"));
	BOOST_TEST(result.stderrContent == "");
	BOOST_TEST(result.options.input.mode == InputMode::Version);
}

BOOST_AUTO_TEST_CASE(multiple_input_modes)
{
	array<string, 10> inputModeOptions = {
		"--help",
		"--license",
		"--version",
		"--standard-json",
		"--link",
		"--assemble",
		"--strict-assembly",
		"--yul",
		"--import-ast",
		"--import-asm-json",
	};
	string expectedMessage =
		"The following options are mutually exclusive: "
		"--help, --license, --version, --standard-json, --link, --assemble, --strict-assembly, --yul, --import-ast, --lsp, --import-asm-json. "
		"Select at most one.";

	for (string const& mode1: inputModeOptions)
		for (string const& mode2: inputModeOptions)
			if (mode1 != mode2)
				BOOST_CHECK_EXCEPTION(
					parseCommandLineAndReadInputFiles({"hypc", mode1, mode2}),
					CommandLineValidationError,
					[&](auto const& _exception) { BOOST_TEST(_exception.what() == expectedMessage); return true; }
				);
}

BOOST_AUTO_TEST_CASE(no_import_callback_allowed_paths)
{
	array<string, 2> options = {
		"--no-import-callback",
		"--allow-paths"
	};

	string expectedMessage =
		"The following options are mutually exclusive: "
		"--no-import-callback, --allow-paths. "
		"Select at most one.";

	BOOST_CHECK_EXCEPTION(
		parseCommandLineAndReadInputFiles({"hypc", options[0], options[1], "."}),
		CommandLineValidationError,
		[&](auto const& _exception) { BOOST_TEST(_exception.what() == expectedMessage); return true; }
	);
}

BOOST_AUTO_TEST_CASE(cli_input)
{
	TemporaryDirectory tempDir1(TEST_CASE_NAME);
	TemporaryDirectory tempDir2(TEST_CASE_NAME);
	createFilesWithParentDirs({tempDir1.path() / "input1.hyp"});
	createFilesWithParentDirs({tempDir2.path() / "input2.hyp"});

	boost::filesystem::path expectedRootPath = FileReader::normalizeCLIRootPathForVFS(tempDir1);
	boost::filesystem::path expectedDir1 = expectedRootPath / tempDir1.path().relative_path();
	boost::filesystem::path expectedDir2 = expectedRootPath / tempDir2.path().relative_path();
	hyptestAssert(expectedDir1.is_absolute() || expectedDir1.root_path() == "/", "");
	hyptestAssert(expectedDir2.is_absolute() || expectedDir2.root_path() == "/", "");

	vector<ImportRemapper::Remapping> expectedRemappings = {
		{"", "a", "b/c/d"},
		{"a", "b", "c/d/e/"},
	};
	map<string, string> expectedSources = {
		{"<stdin>", ""},
		{(expectedDir1 / "input1.hyp").generic_string(), ""},
		{(expectedDir2 / "input2.hyp").generic_string(), ""},
	};
	PathSet expectedAllowedPaths = {
		boost::filesystem::canonical(tempDir1),
		boost::filesystem::canonical(tempDir2),
		"b/c",
		"c/d/e",
	};

	OptionsReaderAndMessages result = parseCommandLineAndReadInputFiles({
		"hypc",
		"a=b/c/d",
		(tempDir1.path() / "input1.hyp").string(),
		(tempDir2.path() / "input2.hyp").string(),
		"a:b=c/d/e/",
		"-",
	});

	BOOST_TEST(result.success);
	BOOST_TEST(result.stderrContent == "");
	BOOST_TEST(result.options.input.mode == InputMode::Compiler);
	BOOST_TEST(result.options.input.addStdin);
	BOOST_CHECK_EQUAL(result.options.input.remappings, expectedRemappings);
	BOOST_CHECK_EQUAL(result.reader.sourceUnits(), expectedSources);
	BOOST_CHECK_EQUAL(result.reader.allowedDirectories(), expectedAllowedPaths);
}

BOOST_AUTO_TEST_CASE(cli_ignore_missing_some_files_exist)
{
	TemporaryDirectory tempDir1(TEST_CASE_NAME);
	TemporaryDirectory tempDir2(TEST_CASE_NAME);
	createFilesWithParentDirs({tempDir1.path() / "input1.hyp"});

	boost::filesystem::path expectedRootPath = FileReader::normalizeCLIRootPathForVFS(tempDir1);
	boost::filesystem::path expectedDir1 = expectedRootPath / tempDir1.path().relative_path();
	hyptestAssert(expectedDir1.is_absolute() || expectedDir1.root_path() == "/", "");

	// NOTE: Allowed paths should not be added for skipped files.
	map<string, string> expectedSources = {{(expectedDir1 / "input1.hyp").generic_string(), ""}};
	PathSet expectedAllowedPaths = {boost::filesystem::canonical(tempDir1)};

	OptionsReaderAndMessages result = parseCommandLineAndReadInputFiles({
		"hypc",
		(tempDir1.path() / "input1.hyp").string(),
		(tempDir2.path() / "input2.hyp").string(),
		"--ignore-missing",
		"--no-color",
	});
	BOOST_TEST(result.success);
	BOOST_TEST(result.stderrContent == "Info: \"" + (tempDir2.path() / "input2.hyp").string() + "\" is not found. Skipping.\n");
	BOOST_TEST(result.options.input.mode == InputMode::Compiler);
	BOOST_TEST(!result.options.input.addStdin);
	BOOST_CHECK_EQUAL(result.reader.sourceUnits(), expectedSources);
	BOOST_CHECK_EQUAL(result.reader.allowedDirectories(), expectedAllowedPaths);
}

BOOST_AUTO_TEST_CASE(cli_ignore_missing_no_files_exist)
{
	TemporaryDirectory tempDir(TEST_CASE_NAME);

	string expectedMessage =
		"Info: \"" + (tempDir.path() / "input1.hyp").string() + "\" is not found. Skipping.\n"
		"Info: \"" + (tempDir.path() / "input2.hyp").string() + "\" is not found. Skipping.\n"
		"Error: All specified input files either do not exist or are not regular files.\n";

	OptionsReaderAndMessages result = runCLI({
		"hypc",
		(tempDir.path() / "input1.hyp").string(),
		(tempDir.path() / "input2.hyp").string(),
		"--ignore-missing",
		"--no-color",
	});
	BOOST_TEST(!result.success);
	BOOST_TEST(result.stderrContent == expectedMessage);
}

BOOST_AUTO_TEST_CASE(cli_not_a_file)
{
	TemporaryDirectory tempDir(TEST_CASE_NAME);

	string expectedMessage = "\"" + tempDir.path().string() + "\" is not a valid file.";

	BOOST_CHECK_EXCEPTION(
		parseCommandLineAndReadInputFiles({"hypc", tempDir.path().string()}),
		CommandLineValidationError,
		[&](auto const& _exception) { BOOST_TEST(_exception.what() == expectedMessage); return true; }
	);
}

BOOST_AUTO_TEST_CASE(standard_json_base_path)
{
	TemporaryDirectory tempDir(TEST_CASE_NAME);
	TemporaryWorkingDirectory tempWorkDir(tempDir.path().root_path());

	OptionsReaderAndMessages result = parseCommandLineAndReadInputFiles({
		"hypc",
		"--standard-json",
		"--base-path=" + tempDir.path().string(),
	});
	BOOST_TEST(result.success);
	BOOST_TEST(result.stderrContent == "");
	BOOST_TEST(result.options.input.mode == InputMode::StandardJson);
	BOOST_TEST(result.options.input.addStdin);
	BOOST_TEST(result.options.input.paths.empty());
	BOOST_TEST(result.reader.sourceUnits().empty());
	BOOST_TEST(result.reader.allowedDirectories().empty());
	BOOST_TEST(result.reader.basePath() == "/" / tempDir.path().relative_path());
}

BOOST_AUTO_TEST_CASE(standard_json_no_input_file)
{
	OptionsReaderAndMessages result = parseCommandLineAndReadInputFiles({"hypc", "--standard-json"});
	BOOST_TEST(result.success);
	BOOST_TEST(result.stderrContent == "");
	BOOST_TEST(result.options.input.mode == InputMode::StandardJson);
	BOOST_TEST(result.options.input.addStdin);
	BOOST_TEST(result.options.input.paths.empty());
	BOOST_TEST(result.reader.sourceUnits().empty());
	BOOST_TEST(result.reader.allowedDirectories().empty());
}

BOOST_AUTO_TEST_CASE(standard_json_dash)
{
	OptionsReaderAndMessages result = parseCommandLineAndReadInputFiles({"hypc", "--standard-json", "-"});
	BOOST_TEST(result.success);
	BOOST_TEST(result.stderrContent == "");
	BOOST_TEST(result.options.input.mode == InputMode::StandardJson);
	BOOST_TEST(result.options.input.addStdin);
	BOOST_TEST(result.reader.sourceUnits().empty());
	BOOST_TEST(result.reader.allowedDirectories().empty());
}

BOOST_AUTO_TEST_CASE(standard_json_one_input_file)
{
	TemporaryDirectory tempDir(TEST_CASE_NAME);
	createFilesWithParentDirs({tempDir.path() / "input.json"});

	vector<string> commandLine = {"hypc", "--standard-json", (tempDir.path() / "input.json").string()};
	OptionsReaderAndMessages result = parseCommandLineAndReadInputFiles(commandLine);
	BOOST_TEST(result.success);
	BOOST_TEST(result.stderrContent == "");
	BOOST_TEST(result.options.input.mode == InputMode::StandardJson);
	BOOST_TEST(!result.options.input.addStdin);
	BOOST_TEST(result.options.input.paths == PathSet{tempDir.path() / "input.json"});
	BOOST_TEST(result.reader.allowedDirectories().empty());
}

BOOST_AUTO_TEST_CASE(standard_json_two_input_files)
{
	string expectedMessage =
		"Too many input files for --standard-json.\n"
		"Please either specify a single file name or provide its content on standard input.";

	BOOST_CHECK_EXCEPTION(
		parseCommandLineAndReadInputFiles({"hypc", "--standard-json", "input1.json", "input2.json"}),
		CommandLineValidationError,
		[&](auto const& _exception) { BOOST_TEST(_exception.what() == expectedMessage); return true; }
	);
}

BOOST_AUTO_TEST_CASE(standard_json_one_input_file_and_stdin)
{
	string expectedMessage =
		"Too many input files for --standard-json.\n"
		"Please either specify a single file name or provide its content on standard input.";

	BOOST_CHECK_EXCEPTION(
		parseCommandLineAndReadInputFiles({"hypc", "--standard-json", "input1.json", "-"}),
		CommandLineValidationError,
		[&](auto const& _exception) { BOOST_TEST(_exception.what() == expectedMessage); return true; }
	);
}

BOOST_AUTO_TEST_CASE(standard_json_ignore_missing)
{
	TemporaryDirectory tempDir(TEST_CASE_NAME);

	// This option is pretty much useless Standard JSON mode.
	string expectedMessage =
		"All specified input files either do not exist or are not regular files.";

	BOOST_CHECK_EXCEPTION(
		parseCommandLineAndReadInputFiles({
			"hypc",
			"--standard-json",
			(tempDir.path() / "input.json").string(),
			"--ignore-missing",
		}),
		CommandLineValidationError,
		[&](auto const& _exception) { BOOST_TEST(_exception.what() == expectedMessage); return true; }
	);
}

BOOST_AUTO_TEST_CASE(standard_json_remapping)
{
	string expectedMessage =
		"Import remappings are not accepted on the command line in Standard JSON mode.\n"
		"Please put them under 'settings.remappings' in the JSON input.";

	BOOST_CHECK_EXCEPTION(
		parseCommandLineAndReadInputFiles({"hypc", "--standard-json", "a=b"}),
		CommandLineValidationError,
		[&](auto const& _exception) { BOOST_TEST(_exception.what() == expectedMessage); return true; }
	);
}

BOOST_AUTO_TEST_CASE(cli_paths_to_source_unit_names_no_base_path)
{
	TemporaryDirectory tempDirCurrent(TEST_CASE_NAME);
	TemporaryDirectory tempDirOther(TEST_CASE_NAME);
	TemporaryWorkingDirectory tempWorkDir(tempDirCurrent);
	hyptestAssert(tempDirCurrent.path().is_absolute(), "");
	hyptestAssert(tempDirOther.path().is_absolute(), "");

	// NOTE: On macOS the path usually contains symlinks which prevents base path from being stripped.
	// Use canonical() to resolve symnlinks and get consistent results on all platforms.
	boost::filesystem::path currentDirNoSymlinks = boost::filesystem::canonical(tempDirCurrent);
	boost::filesystem::path otherDirNoSymlinks = boost::filesystem::canonical(tempDirOther);

	boost::filesystem::path expectedOtherDir = "/" / otherDirNoSymlinks.relative_path();
	hyptestAssert(expectedOtherDir.is_absolute() || expectedOtherDir.root_path() == "/", "");

	vector<string> commandLine = {
		"hypc",
		"contract1.hyp",                                   // Relative path
		"c/d/contract2.hyp",                               // Relative path with subdirectories
		currentDirNoSymlinks.string() + "/contract3.hyp",  // Absolute path inside working dir
		otherDirNoSymlinks.string() + "/contract4.hyp",    // Absolute path outside of working dir
	};

	CommandLineOptions expectedOptions;
	expectedOptions.input.paths = {
		"contract1.hyp",
		"c/d/contract2.hyp",
		currentDirNoSymlinks / "contract3.hyp",
		otherDirNoSymlinks / "contract4.hyp",
	};
	expectedOptions.modelChecker.initialize = true;

	map<string, string> expectedSources = {
		{"contract1.hyp", ""},
		{"c/d/contract2.hyp", ""},
		{"contract3.hyp", ""},
		{expectedOtherDir.generic_string() + "/contract4.hyp", ""},
	};

	FileReader::FileSystemPathSet expectedAllowedDirectories = {
		currentDirNoSymlinks / "c/d",
		currentDirNoSymlinks,
		otherDirNoSymlinks,
	};

	createFilesWithParentDirs(expectedOptions.input.paths);
	OptionsReaderAndMessages result = parseCommandLineAndReadInputFiles(commandLine);

	BOOST_TEST(result.stderrContent == "");
	BOOST_TEST(result.stdoutContent == "");
	BOOST_REQUIRE(result.success);
	BOOST_TEST(result.options == expectedOptions);
	BOOST_TEST(result.reader.sourceUnits() == expectedSources);
	BOOST_TEST(result.reader.allowedDirectories() == expectedAllowedDirectories);
	BOOST_TEST(result.reader.basePath() == "");
}

BOOST_AUTO_TEST_CASE(cli_paths_to_source_unit_names_base_path_same_as_work_dir)
{
	TemporaryDirectory tempDirCurrent(TEST_CASE_NAME);
	TemporaryDirectory tempDirOther(TEST_CASE_NAME);
	TemporaryWorkingDirectory tempWorkDir(tempDirCurrent);
	hyptestAssert(tempDirCurrent.path().is_absolute(), "");
	hyptestAssert(tempDirOther.path().is_absolute(), "");

	// NOTE: On macOS the path usually contains symlinks which prevents base path from being stripped.
	// Use canonical() to resolve symnlinks and get consistent results on all platforms.
	boost::filesystem::path currentDirNoSymlinks = boost::filesystem::canonical(tempDirCurrent);
	boost::filesystem::path otherDirNoSymlinks = boost::filesystem::canonical(tempDirOther);

	boost::filesystem::path expectedWorkDir = "/" / boost::filesystem::current_path().relative_path();
	boost::filesystem::path expectedOtherDir = "/" / otherDirNoSymlinks.relative_path();
	hyptestAssert(expectedWorkDir.is_absolute() || expectedWorkDir.root_path() == "/", "");
	hyptestAssert(expectedOtherDir.is_absolute() || expectedOtherDir.root_path() == "/", "");

	vector<string> commandLine = {
		"hypc",
		"--base-path=" + currentDirNoSymlinks.string(),
		"contract1.hyp",                                   // Relative path
		"c/d/contract2.hyp",                               // Relative path with subdirectories
		currentDirNoSymlinks.string() + "/contract3.hyp",  // Absolute path inside working dir
		otherDirNoSymlinks.string() + "/contract4.hyp",    // Absolute path outside of working dir
	};

	CommandLineOptions expectedOptions;
	expectedOptions.input.paths = {
		"contract1.hyp",
		"c/d/contract2.hyp",
		currentDirNoSymlinks / "contract3.hyp",
		otherDirNoSymlinks / "contract4.hyp",
	};
	expectedOptions.input.basePath = currentDirNoSymlinks;
	expectedOptions.modelChecker.initialize = true;

	map<string, string> expectedSources = {
		{"contract1.hyp", ""},
		{"c/d/contract2.hyp", ""},
		{"contract3.hyp", ""},
		{expectedOtherDir.generic_string() + "/contract4.hyp", ""},
	};

	FileReader::FileSystemPathSet expectedAllowedDirectories = {
		currentDirNoSymlinks / "c/d",
		currentDirNoSymlinks,
		otherDirNoSymlinks,
	};

	createFilesWithParentDirs(expectedOptions.input.paths);
	OptionsReaderAndMessages result = parseCommandLineAndReadInputFiles(commandLine);

	BOOST_TEST(result.stderrContent == "");
	BOOST_TEST(result.stdoutContent == "");
	BOOST_REQUIRE(result.success);
	BOOST_TEST(result.options == expectedOptions);
	BOOST_TEST(result.reader.sourceUnits() == expectedSources);
	BOOST_TEST(result.reader.allowedDirectories() == expectedAllowedDirectories);
	BOOST_TEST(result.reader.basePath() == expectedWorkDir);
}

BOOST_AUTO_TEST_CASE(cli_paths_to_source_unit_names_base_path_different_from_work_dir)
{
	TemporaryDirectory tempDirCurrent(TEST_CASE_NAME);
	TemporaryDirectory tempDirOther(TEST_CASE_NAME);
	TemporaryDirectory tempDirBase(TEST_CASE_NAME);
	TemporaryWorkingDirectory tempWorkDir(tempDirCurrent);
	hyptestAssert(tempDirCurrent.path().is_absolute(), "");
	hyptestAssert(tempDirOther.path().is_absolute(), "");
	hyptestAssert(tempDirBase.path().is_absolute(), "");

	// NOTE: On macOS the path usually contains symlinks which prevents base path from being stripped.
	// Use canonical() to resolve symnlinks and get consistent results on all platforms.
	boost::filesystem::path currentDirNoSymlinks = boost::filesystem::canonical(tempDirCurrent);
	boost::filesystem::path otherDirNoSymlinks = boost::filesystem::canonical(tempDirOther);
	boost::filesystem::path baseDirNoSymlinks = boost::filesystem::canonical(tempDirBase);

	boost::filesystem::path expectedWorkDir = "/" / boost::filesystem::current_path().relative_path();
	boost::filesystem::path expectedCurrentDir = "/" / currentDirNoSymlinks.relative_path();
	boost::filesystem::path expectedOtherDir = "/" / otherDirNoSymlinks.relative_path();
	boost::filesystem::path expectedBaseDir = "/" / baseDirNoSymlinks.relative_path();
	hyptestAssert(expectedWorkDir.is_absolute() || expectedWorkDir.root_path() == "/", "");
	hyptestAssert(expectedCurrentDir.is_absolute() || expectedCurrentDir.root_path() == "/", "");
	hyptestAssert(expectedOtherDir.is_absolute() || expectedOtherDir.root_path() == "/", "");
	hyptestAssert(expectedBaseDir.is_absolute() || expectedBaseDir.root_path() == "/", "");

	vector<string> commandLine = {
		"hypc",
		"--base-path=" + baseDirNoSymlinks.string(),
		"contract1.hyp",                                   // Relative path
		"c/d/contract2.hyp",                               // Relative path with subdirectories
		currentDirNoSymlinks.string() + "/contract3.hyp",  // Absolute path inside working dir
		otherDirNoSymlinks.string() + "/contract4.hyp",    // Absolute path outside of working dir
		baseDirNoSymlinks.string() + "/contract5.hyp",     // Absolute path inside base path
	};

	CommandLineOptions expectedOptions;
	expectedOptions.input.paths = {
		"contract1.hyp",
		"c/d/contract2.hyp",
		currentDirNoSymlinks / "contract3.hyp",
		otherDirNoSymlinks / "contract4.hyp",
		baseDirNoSymlinks / "contract5.hyp",
	};
	expectedOptions.input.basePath = baseDirNoSymlinks;
	expectedOptions.modelChecker.initialize = true;

	map<string, string> expectedSources = {
		{expectedWorkDir.generic_string() + "/contract1.hyp", ""},
		{expectedWorkDir.generic_string() + "/c/d/contract2.hyp", ""},
		{expectedCurrentDir.generic_string() + "/contract3.hyp", ""},
		{expectedOtherDir.generic_string() + "/contract4.hyp", ""},
		{"contract5.hyp", ""},
	};

	FileReader::FileSystemPathSet expectedAllowedDirectories = {
		currentDirNoSymlinks / "c/d",
		currentDirNoSymlinks,
		otherDirNoSymlinks,
		baseDirNoSymlinks,
	};

	createFilesWithParentDirs(expectedOptions.input.paths);
	OptionsReaderAndMessages result = parseCommandLineAndReadInputFiles(commandLine);

	BOOST_TEST(result.stderrContent == "");
	BOOST_TEST(result.stdoutContent == "");
	BOOST_REQUIRE(result.success);
	BOOST_TEST(result.options == expectedOptions);
	BOOST_TEST(result.reader.sourceUnits() == expectedSources);
	BOOST_TEST(result.reader.allowedDirectories() == expectedAllowedDirectories);
	BOOST_TEST(result.reader.basePath() == expectedBaseDir);
}

BOOST_AUTO_TEST_CASE(cli_paths_to_source_unit_names_relative_base_path)
{
	TemporaryDirectory tempDirCurrent(TEST_CASE_NAME);
	TemporaryDirectory tempDirOther(TEST_CASE_NAME);
	TemporaryWorkingDirectory tempWorkDir(tempDirCurrent);
	hyptestAssert(tempDirCurrent.path().is_absolute(), "");
	hyptestAssert(tempDirOther.path().is_absolute(), "");

	// NOTE: On macOS the path usually contains symlinks which prevents base path from being stripped.
	// Use canonical() to resolve symnlinks and get consistent results on all platforms.
	boost::filesystem::path currentDirNoSymlinks = boost::filesystem::canonical(tempDirCurrent);
	boost::filesystem::path otherDirNoSymlinks = boost::filesystem::canonical(tempDirOther);

	boost::filesystem::path expectedWorkDir = "/" / boost::filesystem::current_path().relative_path();
	boost::filesystem::path expectedOtherDir = "/" / otherDirNoSymlinks.relative_path();
	hyptestAssert(expectedWorkDir.is_absolute() || expectedWorkDir.root_path() == "/", "");
	hyptestAssert(expectedOtherDir.is_absolute() || expectedOtherDir.root_path() == "/", "");

	vector<string> commandLine = {
		"hypc",
		"--base-path=base",
		"contract1.hyp",                                       // Relative path outside of base path
		"base/contract2.hyp",                                  // Relative path inside base path
		currentDirNoSymlinks.string() + "/contract3.hyp",      // Absolute path inside working dir
		currentDirNoSymlinks.string() + "/base/contract4.hyp", // Absolute path inside base path
		otherDirNoSymlinks.string() + "/contract5.hyp",        // Absolute path outside of working dir
		otherDirNoSymlinks.string() + "/base/contract6.hyp",   // Absolute path outside of working dir
	};

	CommandLineOptions expectedOptions;
	expectedOptions.input.paths = {
		"contract1.hyp",
		"base/contract2.hyp",
		currentDirNoSymlinks / "contract3.hyp",
		currentDirNoSymlinks / "base/contract4.hyp",
		otherDirNoSymlinks / "contract5.hyp",
		otherDirNoSymlinks / "base/contract6.hyp",
	};
	expectedOptions.input.basePath = "base";
	expectedOptions.modelChecker.initialize = true;

	map<string, string> expectedSources = {
		{expectedWorkDir.generic_string() + "/contract1.hyp", ""},
		{"contract2.hyp", ""},
		{expectedWorkDir.generic_string() + "/contract3.hyp", ""},
		{"contract4.hyp", ""},
		{expectedOtherDir.generic_string() + "/contract5.hyp", ""},
		{expectedOtherDir.generic_string() + "/base/contract6.hyp", ""},
	};

	FileReader::FileSystemPathSet expectedAllowedDirectories = {
		currentDirNoSymlinks / "base",
		currentDirNoSymlinks,
		otherDirNoSymlinks,
		otherDirNoSymlinks / "base",
	};

	createFilesWithParentDirs(expectedOptions.input.paths);
	OptionsReaderAndMessages result = parseCommandLineAndReadInputFiles(commandLine);

	BOOST_TEST(result.stderrContent == "");
	BOOST_TEST(result.stdoutContent == "");
	BOOST_REQUIRE(result.success);
	BOOST_TEST(result.options == expectedOptions);
	BOOST_TEST(result.reader.sourceUnits() == expectedSources);
	BOOST_TEST(result.reader.allowedDirectories() == expectedAllowedDirectories);
	BOOST_TEST(result.reader.basePath() == expectedWorkDir / "base");
}

BOOST_AUTO_TEST_CASE(cli_paths_to_source_unit_names_normalization_and_weird_names)
{
	TemporaryDirectory tempDir({"x/y/z"}, TEST_CASE_NAME);
	TemporaryWorkingDirectory tempWorkDir(tempDir.path() / "x/y/z");
	hyptestAssert(tempDir.path().is_absolute(), "");

	string uncPath = "//" + tempDir.path().relative_path().generic_string();
	hyptestAssert(FileReader::isUNCPath(uncPath), "");

	boost::filesystem::path tempDirNoSymlinks = boost::filesystem::canonical(tempDir);

	boost::filesystem::path expectedWorkDir = "/" / boost::filesystem::current_path().relative_path();
	hyptestAssert(expectedWorkDir.is_absolute() || expectedWorkDir.root_path() == "/", "");

	vector<string> commandLine = {
		"hypc",

#if !defined(_WIN32)
		// URLs. We interpret them as local paths.
		// Note that : is not allowed in file names on Windows.
		"file://c/d/contract1.hyp",
		"file:///c/d/contract2.hyp",
		"https://example.com/contract3.hyp",
#endif

		// Redundant slashes
		"a/b//contract4.hyp",
		"a/b///contract5.hyp",
		"a/b////contract6.hyp",

		// Dot segments
		"./a/b/contract7.hyp",
		"././a/b/contract8.hyp",
		"a/./b/contract9.hyp",
		"a/././b/contract10.hyp",

		// Dot dot segments
		"../a/b/contract11.hyp",
		"../../a/b/contract12.hyp",
		"a/../b/contract13.hyp",
		"a/b/../../contract14.hyp",
		tempDirNoSymlinks.string() + "/x/y/z/a/../b/contract15.hyp",
		tempDirNoSymlinks.string() + "/x/y/z/a/b/../../contract16.hyp",

		// Dot dot segments going beyond filesystem root
		"/../" + tempDir.path().relative_path().generic_string() + "/contract17.hyp",
		"/../../" + tempDir.path().relative_path().generic_string() + "/contract18.hyp",

#if !defined(_WIN32)
		// Name conflict with source unit name of stdin.
		// Note that < and > are not allowed in file names on Windows.
		"<stdin>",

		// UNC paths on UNIX just resolve into normal paths. On Windows this would be an network
		// share (and an error unless the share actually exists so I can't test it here).
		uncPath + "/contract19.hyp",

		// Windows paths on non-Windows systems.
		// Note that on Windows we tested them already just by using absolute paths.
		"a\\b\\contract20.hyp",
		"C:\\a\\b\\contract21.hyp",
#endif
	};

	CommandLineOptions expectedOptions;
	expectedOptions.input.paths = {
#if !defined(_WIN32)
		"file://c/d/contract1.hyp",
		"file:///c/d/contract2.hyp",
		"https://example.com/contract3.hyp",
#endif

		"a/b//contract4.hyp",
		"a/b///contract5.hyp",
		"a/b////contract6.hyp",

		"./a/b/contract7.hyp",
		"././a/b/contract8.hyp",
		"a/./b/contract9.hyp",
		"a/././b/contract10.hyp",

		"../a/b/contract11.hyp",
		"../../a/b/contract12.hyp",
		"a/../b/contract13.hyp",
		"a/b/../../contract14.hyp",
		tempDirNoSymlinks.string() + "/x/y/z/a/../b/contract15.hyp",
		tempDirNoSymlinks.string() + "/x/y/z/a/b/../../contract16.hyp",

		"/../" + tempDir.path().relative_path().string() + "/contract17.hyp",
		"/../../" + tempDir.path().relative_path().string() + "/contract18.hyp",

#if !defined(_WIN32)
		"<stdin>",

		uncPath + "/contract19.hyp",

		"a\\b\\contract20.hyp",
		"C:\\a\\b\\contract21.hyp",
#endif
	};
	expectedOptions.modelChecker.initialize = true;

	map<string, string> expectedSources = {
#if !defined(_WIN32)
		{"file:/c/d/contract1.hyp", ""},
		{"file:/c/d/contract2.hyp", ""},
		{"https:/example.com/contract3.hyp", ""},
#endif

		{"a/b/contract4.hyp", ""},
		{"a/b/contract5.hyp", ""},
		{"a/b/contract6.hyp", ""},

		{"a/b/contract7.hyp", ""},
		{"a/b/contract8.hyp", ""},
		{"a/b/contract9.hyp", ""},
		{"a/b/contract10.hyp", ""},

		{expectedWorkDir.parent_path().generic_string() + "/a/b/contract11.hyp", ""},
		{expectedWorkDir.parent_path().parent_path().generic_string() + "/a/b/contract12.hyp", ""},
		{"b/contract13.hyp", ""},
		{"contract14.hyp", ""},
		{"b/contract15.hyp", ""},
		{"contract16.hyp", ""},

		{"/" + tempDir.path().relative_path().generic_string() + "/contract17.hyp", ""},
		{"/" + tempDir.path().relative_path().generic_string() + "/contract18.hyp", ""},

#if !defined(_WIN32)
		{"<stdin>", ""},

		{uncPath + "/contract19.hyp", ""},

		{"a\\b\\contract20.hyp", ""},
		{"C:\\a\\b\\contract21.hyp", ""},
#endif
	};

	FileReader::FileSystemPathSet expectedAllowedDirectories = {
#if !defined(_WIN32)
		tempDirNoSymlinks / "x/y/z/file:/c/d",
		tempDirNoSymlinks / "x/y/z/https:/example.com",
#endif
		tempDirNoSymlinks / "x/y/z/a/b",
		tempDirNoSymlinks / "x/y/z",
		tempDirNoSymlinks / "x/y/z/b",
		tempDirNoSymlinks / "x/y/a/b",
		tempDirNoSymlinks / "x/a/b",
		tempDirNoSymlinks,
#if !defined(_WIN32)
		boost::filesystem::canonical(uncPath),
#endif
	};

	createFilesWithParentDirs(expectedOptions.input.paths);

	OptionsReaderAndMessages result = parseCommandLineAndReadInputFiles(commandLine);

	BOOST_TEST(result.stderrContent == "");
	BOOST_TEST(result.stdoutContent == "");
	BOOST_REQUIRE(result.success);
	BOOST_TEST(result.options == expectedOptions);
	BOOST_TEST(result.reader.sourceUnits() == expectedSources);
	BOOST_TEST(result.reader.allowedDirectories() == expectedAllowedDirectories);
	BOOST_TEST(result.reader.basePath() == expectedOptions.input.basePath);
}

BOOST_AUTO_TEST_CASE(cli_paths_to_source_unit_names_symlinks)
{
	TemporaryDirectory tempDir({"r/"}, TEST_CASE_NAME);
	createFilesWithParentDirs({tempDir.path() / "x/y/z/contract.hyp"});
	TemporaryWorkingDirectory tempWorkDir(tempDir.path() / "r");

	if (
		!createSymlinkIfSupportedByFilesystem("../x/y", tempDir.path() / "r/sym", true) ||
		!createSymlinkIfSupportedByFilesystem("contract.hyp", tempDir.path() / "x/y/z/contract_symlink.hyp", false)
	)
		return;

	boost::filesystem::path expectedWorkDir = "/" / boost::filesystem::current_path().relative_path();
	hyptestAssert(expectedWorkDir.is_absolute() || expectedWorkDir.root_path() == "/", "");

	vector<string> commandLine = {
		"hypc",

		"--base-path=../r/sym/z/",
		"sym/z/contract.hyp",            // File accessed directly + same dir symlink as base path
		"../x/y/z/contract.hyp",         // File accessed directly + different dir symlink than base path
		"sym/z/contract_symlink.hyp",    // File accessed via symlink + same dir symlink as base path
		"../x/y/z/contract_symlink.hyp", // File accessed via symlink + different dir symlink than base path
	};

	CommandLineOptions expectedOptions;
	expectedOptions.input.paths = {
		"sym/z/contract.hyp",
		"../x/y/z/contract.hyp",
		"sym/z/contract_symlink.hyp",
		"../x/y/z/contract_symlink.hyp",
	};
	expectedOptions.input.basePath = "../r/sym/z/";
	expectedOptions.modelChecker.initialize = true;

	map<string, string> expectedSources = {
		{"contract.hyp", ""},
		{(expectedWorkDir.parent_path() / "x/y/z/contract.hyp").generic_string(), ""},
		{"contract_symlink.hyp", ""},
		{(expectedWorkDir.parent_path() / "x/y/z/contract_symlink.hyp").generic_string(), ""},
	};

	FileReader::FileSystemPathSet expectedAllowedDirectories = {
		boost::filesystem::canonical(tempDir) / "x/y/z",
	};

	OptionsReaderAndMessages result = parseCommandLineAndReadInputFiles(commandLine);

	BOOST_TEST(result.stderrContent == "");
	BOOST_TEST(result.stdoutContent == "");
	BOOST_REQUIRE(result.success);
	BOOST_TEST(result.options == expectedOptions);
	BOOST_TEST(result.reader.sourceUnits() == expectedSources);
	BOOST_TEST(result.reader.allowedDirectories() == expectedAllowedDirectories);
	BOOST_TEST(result.reader.basePath() == expectedWorkDir / "sym/z/");
}

BOOST_AUTO_TEST_CASE(cli_paths_to_source_unit_names_base_path_and_stdin)
{
	TemporaryDirectory tempDir(TEST_CASE_NAME);
	TemporaryWorkingDirectory tempWorkDir(tempDir);
	boost::filesystem::create_directories(tempDir.path() / "base");

	boost::filesystem::path expectedWorkDir = "/" / boost::filesystem::current_path().relative_path();

	vector<string> commandLine = {"hypc", "--base-path=base", "-"};

	CommandLineOptions expectedOptions;
	expectedOptions.input.addStdin = true;
	expectedOptions.input.basePath = "base";
	expectedOptions.modelChecker.initialize = true;

	map<string, string> expectedSources = {
		{"<stdin>", ""},
	};
	FileReader::FileSystemPathSet expectedAllowedDirectories = {};

	createFilesWithParentDirs(expectedOptions.input.paths);
	OptionsReaderAndMessages result = parseCommandLineAndReadInputFiles(commandLine);

	BOOST_TEST(result.stderrContent == "");
	BOOST_TEST(result.stdoutContent == "");
	BOOST_REQUIRE(result.success);
	BOOST_TEST(result.options == expectedOptions);
	BOOST_TEST(result.reader.sourceUnits() == expectedSources);
	BOOST_TEST(result.reader.allowedDirectories() == expectedAllowedDirectories);
	BOOST_TEST(result.reader.basePath() == expectedWorkDir / "base");
}

BOOST_AUTO_TEST_CASE(cli_include_paths)
{
	TemporaryDirectory tempDir({"base/", "include/", "lib/nested/"}, TEST_CASE_NAME);
	TemporaryWorkingDirectory tempWorkDir(tempDir);

	string const mainContractSource = withPreamble(
		"import \"contract.hyp\";\n"
		"import \"contract_via_callback.hyp\";\n"
		"import \"include.hyp\";\n"
		"import \"include_via_callback.hyp\";\n"
		"import \"nested.hyp\";\n"
		"import \"nested_via_callback.hyp\";\n"
		"import \"lib.hyp\";\n"
		"import \"lib_via_callback.hyp\";\n"
	);

	string const onlyPreamble = withPreamble("");
	createFilesWithParentDirs(
		{
			tempDir.path() / "base/contract.hyp",
			tempDir.path() / "base/contract_via_callback.hyp",
			tempDir.path() / "include/include.hyp",
			tempDir.path() / "include/include_via_callback.hyp",
			tempDir.path() / "lib/nested/nested.hyp",
			tempDir.path() / "lib/nested/nested_via_callback.hyp",
			tempDir.path() / "lib/lib.hyp",
			tempDir.path() / "lib/lib_via_callback.hyp",
		},
		onlyPreamble
	);
	createFilesWithParentDirs({tempDir.path() / "base/main.hyp"}, mainContractSource);

	boost::filesystem::path canonicalWorkDir = boost::filesystem::canonical(tempDir);
	boost::filesystem::path expectedWorkDir = "/" / canonicalWorkDir.relative_path();

	vector<string> commandLine = {
		"hypc",
		"--no-color",
		"--base-path=base/",
		"--include-path=include/",
		"--include-path=lib/nested",
		"--include-path=lib/",
		"base/main.hyp",
		"base/contract.hyp",
		"include/include.hyp",
		"lib/nested/nested.hyp",
		"lib/lib.hyp",
	};

	CommandLineOptions expectedOptions;
	expectedOptions.input.paths = {
		"base/main.hyp",
		"base/contract.hyp",
		"include/include.hyp",
		"lib/nested/nested.hyp",
		"lib/lib.hyp",
	};
	expectedOptions.input.basePath = "base/";
	expectedOptions.input.includePaths = {
		"include/",
		"lib/nested",
		"lib/",
	};
	expectedOptions.formatting.coloredOutput = false;
	expectedOptions.modelChecker.initialize = true;

	map<string, string> expectedSources = {
		{"main.hyp", mainContractSource},
		{"contract.hyp", onlyPreamble},
		{"contract_via_callback.hyp", onlyPreamble},
		{"include.hyp", onlyPreamble},
		{"include_via_callback.hyp", onlyPreamble},
		{"nested.hyp", onlyPreamble},
		{"nested_via_callback.hyp", onlyPreamble},
		{"lib.hyp", onlyPreamble},
		{"lib_via_callback.hyp", onlyPreamble},
	};

	vector<boost::filesystem::path> expectedIncludePaths = {
		expectedWorkDir / "include/",
		expectedWorkDir / "lib/nested",
		expectedWorkDir / "lib/",
	};

	FileReader::FileSystemPathSet expectedAllowedDirectories = {
		canonicalWorkDir / "base",
		canonicalWorkDir / "include",
		canonicalWorkDir / "lib/nested",
		canonicalWorkDir / "lib",
	};

	string const expectedStdoutContent = "Compiler run successful. No contracts to compile.\n";
	OptionsReaderAndMessages result = runCLI(commandLine, "");

	BOOST_TEST(result.stderrContent == "");
	if (SemVerVersion{string(VersionString)}.isPrerelease())
		BOOST_TEST(result.stdoutContent == "");
	else
		BOOST_TEST(result.stdoutContent == expectedStdoutContent);
	BOOST_REQUIRE(result.success);
	BOOST_TEST(result.options == expectedOptions);
	BOOST_TEST(result.reader.sourceUnits() == expectedSources);
	BOOST_TEST(result.reader.includePaths() == expectedIncludePaths);
	BOOST_TEST(result.reader.allowedDirectories() == expectedAllowedDirectories);
	BOOST_TEST(result.reader.basePath() == expectedWorkDir / "base/");
}

BOOST_AUTO_TEST_CASE(cli_no_contracts_to_compile)
{
	string const contractSource = R"(
		// SPDX-License-Identifier: GPL-3.0
		pragma hyperion >=0.0;
		enum Status { test }
	)";

	string const expectedStdoutContent = "Compiler run successful. No contracts to compile.\n";
	OptionsReaderAndMessages result = runCLI({"hypc", "-"}, contractSource);

	if (SemVerVersion{string(VersionString)}.isPrerelease())
		BOOST_TEST(result.stdoutContent == "");
	else
		BOOST_TEST(result.stdoutContent == expectedStdoutContent);
	BOOST_REQUIRE(result.success);
}

BOOST_AUTO_TEST_CASE(cli_no_output)
{
	string const contractSource = R"(
		// SPDX-License-Identifier: GPL-3.0
		pragma hyperion >=0.0;
		abstract contract A {
			function B() public virtual returns(uint);
		})";

	string const expectedStdoutContent = "Compiler run successful. No output generated.\n";
	OptionsReaderAndMessages result = runCLI({"hypc", "-"}, contractSource);

	if (SemVerVersion{string(VersionString)}.isPrerelease())
		BOOST_TEST(result.stdoutContent == "");
	else
		BOOST_TEST(result.stdoutContent == expectedStdoutContent);
	BOOST_REQUIRE(result.success);
}

BOOST_AUTO_TEST_CASE(standard_json_include_paths)
{
	TemporaryDirectory tempDir({"base/", "include/", "lib/nested/"}, TEST_CASE_NAME);
	TemporaryWorkingDirectory tempWorkDir(tempDir);

	string const mainContractSource = withPreamble(
		"import 'contract_via_callback.hyp';\n"
		"import 'include_via_callback.hyp';\n"
		"import 'nested_via_callback.hyp';\n"
		"import 'lib_via_callback.hyp';\n"
	);

	string const standardJsonInput = R"(
		{
			"language": "Hyperion",
			"sources": {
				"main.hyp": {"content": ")" + mainContractSource + R"("}
			}
		}
	)";

	string const onlyPreamble = withPreamble("");
	createFilesWithParentDirs(
		{
			tempDir.path() / "base/contract_via_callback.hyp",
			tempDir.path() / "include/include_via_callback.hyp",
			tempDir.path() / "lib/nested/nested_via_callback.hyp",
			tempDir.path() / "lib/lib_via_callback.hyp",
		},
		onlyPreamble
	);

	boost::filesystem::path expectedWorkDir = "/" / boost::filesystem::canonical(tempDir).relative_path();

	vector<string> commandLine = {
		"hypc",
		"--base-path=base/",
		"--include-path=include/",
		"--include-path=lib/nested",
		"--include-path=lib/",
		"--standard-json",
	};

	CommandLineOptions expectedOptions;
	expectedOptions.input.mode = InputMode::StandardJson;
	expectedOptions.input.paths = {};
	expectedOptions.input.addStdin = true;
	expectedOptions.input.basePath = "base/";
	expectedOptions.input.includePaths = {
		"include/",
		"lib/nested",
		"lib/",
	};
	expectedOptions.modelChecker.initialize = false;

	// NOTE: Source code from Standard JSON does not end up in FileReader. This is not a problem
	// because FileReader is only used once to initialize the compiler stack and after that
	// its sources are irrelevant (even though the callback still stores everything it loads).
	map<string, string> expectedSources = {
		{"contract_via_callback.hyp", onlyPreamble},
		{"include_via_callback.hyp", onlyPreamble},
		{"nested_via_callback.hyp", onlyPreamble},
		{"lib_via_callback.hyp", onlyPreamble},
	};

	vector<boost::filesystem::path> expectedIncludePaths = {
		expectedWorkDir / "include/",
		expectedWorkDir / "lib/nested",
		expectedWorkDir / "lib/",
	};

	FileReader::FileSystemPathSet expectedAllowedDirectories = {};

	OptionsReaderAndMessages result = runCLI(commandLine, standardJsonInput);

	Json::Value parsedStdout;
	string jsonParsingErrors;
	BOOST_TEST(util::jsonParseStrict(result.stdoutContent, parsedStdout, &jsonParsingErrors));
	BOOST_TEST(jsonParsingErrors == "");
	for (Json::Value const& errorDict: parsedStdout["errors"])
		// The error list might contain pre-release compiler warning
		BOOST_TEST(errorDict["severity"] != "error");
	BOOST_TEST(
		(parsedStdout["sources"].getMemberNames() | ranges::to<set>) ==
		(expectedSources | ranges::views::keys | ranges::to<set>) + set<string>{"main.hyp"}
	);

	BOOST_REQUIRE(result.success);
	BOOST_TEST(result.options == expectedOptions);
	BOOST_TEST(result.reader.sourceUnits() == expectedSources);
	BOOST_TEST(result.reader.includePaths() == expectedIncludePaths);
	BOOST_TEST(result.reader.allowedDirectories() == expectedAllowedDirectories);
	BOOST_TEST(result.reader.basePath() == expectedWorkDir / "base/");
}

BOOST_AUTO_TEST_CASE(cli_include_paths_empty_path)
{
	TemporaryDirectory tempDir({"base/", "include/"}, TEST_CASE_NAME);
	TemporaryWorkingDirectory tempWorkDir(tempDir);
	createFilesWithParentDirs({tempDir.path() / "base/main.hyp"});

	string expectedMessage = "Empty values are not allowed in --include-path.";

	BOOST_CHECK_EXCEPTION(
		parseCommandLineAndReadInputFiles({
			"hypc",
			"--base-path=base/",
			"--include-path", "include/",
			"--include-path", "",
			"base/main.hyp",
		}),
		CommandLineValidationError,
		[&](auto const& _exception) { BOOST_TEST(_exception.what() == expectedMessage); return true; }
	);
}

BOOST_AUTO_TEST_CASE(cli_include_paths_without_base_path)
{
	TemporaryDirectory tempDir(TEST_CASE_NAME);
	TemporaryWorkingDirectory tempWorkDir(tempDir);
	createFilesWithParentDirs({tempDir.path() / "contract.hyp"});

	string expectedMessage = "--include-path option requires a non-empty base path.";

	BOOST_CHECK_EXCEPTION(
		parseCommandLineAndReadInputFiles({"hypc", "--include-path", "include/", "contract.hyp"}),
		CommandLineValidationError,
		[&](auto const& _exception) { BOOST_TEST(_exception.what() == expectedMessage); return true; }
	);
}

BOOST_AUTO_TEST_CASE(cli_include_paths_should_detect_source_unit_name_collisions)
{
	TemporaryDirectory tempDir({"dir1/", "dir2/", "dir3/"}, TEST_CASE_NAME);
	TemporaryWorkingDirectory tempWorkDir(tempDir);
	createFilesWithParentDirs({
		"dir1/contract1.hyp",
		"dir1/contract2.hyp",
		"dir2/contract1.hyp",
		"dir2/contract2.hyp",
	});

	boost::filesystem::path expectedWorkDir = "/" / boost::filesystem::canonical(tempDir).relative_path();

	string expectedMessage =
		"Source unit name collision detected. "
		"The specified values of base path and/or include paths would result in multiple "
		"input files being assigned the same source unit name:\n"
		"contract1.hyp matches: "
		"\"" + (expectedWorkDir / "dir1/contract1.hyp").generic_string() + "\", "
		"\"" + (expectedWorkDir / "dir2/contract1.hyp").generic_string() + "\"\n"
		"contract2.hyp matches: "
		"\"" + (expectedWorkDir / "dir1/contract2.hyp").generic_string() + "\", "
		"\"" + (expectedWorkDir / "dir2/contract2.hyp").generic_string() + "\"\n";

	{
		// import "contract1.hyp" and import "contract2.hyp" would be ambiguous:
		BOOST_CHECK_EXCEPTION(
			parseCommandLineAndReadInputFiles({
				"hypc",
				"--base-path=dir1/",
				"--include-path=dir2/",
				"dir1/contract1.hyp",
				"dir2/contract1.hyp",
				"dir1/contract2.hyp",
				"dir2/contract2.hyp",
			}),
			CommandLineValidationError,
			[&](auto const& _exception) { BOOST_TEST(_exception.what() == expectedMessage); return true; }
		);
	}

	{
		// import "contract1.hyp" and import "contract2.hyp" would be ambiguous:
		BOOST_CHECK_EXCEPTION(
			parseCommandLineAndReadInputFiles({
				"hypc",
				"--base-path=dir3/",
				"--include-path=dir1/",
				"--include-path=dir2/",
				"dir1/contract1.hyp",
				"dir2/contract1.hyp",
				"dir1/contract2.hyp",
				"dir2/contract2.hyp",
			}),
			CommandLineValidationError,
			[&](auto const& _exception) { BOOST_TEST(_exception.what() == expectedMessage); return true; }
		);
	}

	{
		// No conflict if files with the same name exist but only one is given to the compiler.
		vector<string> commandLine = {
			"hypc",
			"--base-path=dir3/",
			"--include-path=dir1/",
			"--include-path=dir2/",
			"dir1/contract1.hyp",
			"dir1/contract2.hyp",
		};
		OptionsReaderAndMessages result = parseCommandLineAndReadInputFiles(commandLine);
		BOOST_TEST(result.stderrContent == "");
		BOOST_REQUIRE(result.success);
	}

	{
		// The same file specified multiple times is not a conflict.
		vector<string> commandLine = {
			"hypc",
			"--base-path=dir3/",
			"--include-path=dir1/",
			"--include-path=dir2/",
			"dir1/contract1.hyp",
			"dir1/contract1.hyp",
			"./dir1/contract1.hyp",
		};
		OptionsReaderAndMessages result = parseCommandLineAndReadInputFiles(commandLine);
		BOOST_TEST(result.stderrContent == "");
		BOOST_REQUIRE(result.success);
	}
}

BOOST_AUTO_TEST_CASE(cli_include_paths_should_allow_duplicate_paths)
{
	TemporaryDirectory tempDir({"dir1/", "dir2/"}, TEST_CASE_NAME);
	TemporaryWorkingDirectory tempWorkDir(tempDir);
	createFilesWithParentDirs({"dir1/contract.hyp"});

	boost::filesystem::path expectedWorkDir = "/" / boost::filesystem::canonical(tempDir).relative_path();
	boost::filesystem::path expectedTempDir = "/" / tempDir.path().relative_path();

	vector<string> commandLine = {
		"hypc",
		"--base-path=dir1/",
		"--include-path", "dir1",
		"--include-path", "dir1",
		"--include-path", "dir1/",
		"--include-path", "dir1/",
		"--include-path", "./dir1/",
		"--include-path", "dir2/../dir1/",
		"--include-path", (tempDir.path() / "dir1/").string(),
		"--include-path", (expectedWorkDir / "dir1/").string(),
		"--include-path", "dir1/",
		"dir1/contract.hyp",
	};

	// Duplicates do not affect the result but are not removed from the include path list.
	vector<boost::filesystem::path> expectedIncludePaths = {
		expectedWorkDir / "dir1",
		expectedWorkDir / "dir1",
		expectedWorkDir / "dir1/",
		expectedWorkDir / "dir1/",
		expectedWorkDir / "dir1/",
		expectedWorkDir / "dir1/",
		// NOTE: On macOS expectedTempDir usually contains a symlink and therefore for us it's
		// different from expectedWorkDir.
		expectedTempDir / "dir1/",
		expectedWorkDir / "dir1/",
		expectedWorkDir / "dir1/",
	};

	OptionsReaderAndMessages result = parseCommandLineAndReadInputFiles(commandLine);
	BOOST_TEST(result.stderrContent == "");
	BOOST_REQUIRE(result.success);
	BOOST_TEST(result.reader.includePaths() == expectedIncludePaths);
	BOOST_TEST(result.reader.basePath() == expectedWorkDir / "dir1/");
}

BOOST_AUTO_TEST_CASE(cli_include_paths_ambiguous_import)
{
	TemporaryDirectory tempDir({"base/", "include/"}, TEST_CASE_NAME);
	TemporaryWorkingDirectory tempWorkDir(tempDir);

	// Ambiguous: both base/contract.hyp and include/contract.hyp match the import.
	string const mainContractSource = withPreamble("import \"contract.hyp\";");

	createFilesWithParentDirs({"base/contract.hyp", "include/contract.hyp"}, withPreamble(""));

	boost::filesystem::path expectedWorkDir = "/" / boost::filesystem::canonical(tempDir).relative_path();

	vector<string> commandLine = {
		"hypc",
		"--no-color",
		"--base-path=base/",
		"--include-path=include/",
		"-",
	};

	string expectedMessage =
		"Error: Source \"contract.hyp\" not found: Ambiguous import. "
		"Multiple matching files found inside base path and/or include paths: \"" +
		(expectedWorkDir / "base/contract.hyp").generic_string() + "\", \"" +
		(expectedWorkDir / "include/contract.hyp").generic_string() + "\".\n"
		" --> <stdin>:3:1:\n"
		"  |\n"
		"3 | import \"contract.hyp\";\n"
		"  | ^^^^^^^^^^^^^^^^^^^^^^\n\n";

	OptionsReaderAndMessages result = runCLI(commandLine, mainContractSource);
	BOOST_TEST(result.stderrContent == expectedMessage);
	BOOST_REQUIRE(!result.success);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace hyperion::frontend::test
