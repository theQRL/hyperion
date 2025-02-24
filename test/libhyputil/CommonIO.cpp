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

/// Unit tests for the CommonIO routines.

#include <libhyputil/CommonIO.h>
#include <libhyputil/TemporaryDirectory.h>

#include <test/Common.h>
#include <test/FilesystemUtils.h>
#include <test/libhyperion/util/HyptestErrors.h>

#include <boost/test/unit_test.hpp>
#include <boost/filesystem.hpp>

#include <fstream>
#include <string>

using namespace hyperion::test;

#define TEST_CASE_NAME (boost::unit_test::framework::current_test_case().p_name)

namespace hyperion::util::test
{

BOOST_AUTO_TEST_SUITE(CommonIOTest)

BOOST_AUTO_TEST_CASE(readFileAsString_regular_file)
{
	TemporaryDirectory tempDir(TEST_CASE_NAME);
	createFileWithContent(tempDir.path() / "test.txt", "ABC\ndef\n");

	BOOST_TEST(readFileAsString(tempDir.path() / "test.txt") == "ABC\ndef\n");
}

BOOST_AUTO_TEST_CASE(readFileAsString_directory)
{
	TemporaryDirectory tempDir(TEST_CASE_NAME);
	BOOST_CHECK_THROW(readFileAsString(tempDir), NotAFile);
}

BOOST_AUTO_TEST_CASE(readFileAsString_symlink)
{
	TemporaryDirectory tempDir(TEST_CASE_NAME);
	createFileWithContent(tempDir.path() / "test.txt", "ABC\ndef\n");

	if (!createSymlinkIfSupportedByFilesystem("test.txt", tempDir.path() / "symlink.txt", false))
		return;

	BOOST_TEST(readFileAsString(tempDir.path() / "symlink.txt") == "ABC\ndef\n");
}

BOOST_AUTO_TEST_CASE(readUntilEnd_no_ending_newline)
{
	std::istringstream inputStream("ABC\ndef");
	BOOST_TEST(readUntilEnd(inputStream) == "ABC\ndef");
}

BOOST_AUTO_TEST_CASE(readUntilEnd_with_ending_newline)
{
	std::istringstream inputStream("ABC\ndef\n");
	BOOST_TEST(readUntilEnd(inputStream) == "ABC\ndef\n");
}

BOOST_AUTO_TEST_CASE(readUntilEnd_cr_lf_newline)
{
	std::istringstream inputStream("ABC\r\ndef");
	BOOST_TEST(readUntilEnd(inputStream) == "ABC\r\ndef");
}

BOOST_AUTO_TEST_CASE(readUntilEnd_empty)
{
	std::istringstream inputStream("");
	BOOST_TEST(readUntilEnd(inputStream) == "");
}

BOOST_AUTO_TEST_CASE(readBytes_past_end)
{
	std::istringstream inputStream("abc");
	BOOST_CHECK_EQUAL(readBytes(inputStream, 0), "");
	BOOST_CHECK_EQUAL(readBytes(inputStream, 1), "a");
	BOOST_CHECK_EQUAL(readBytes(inputStream, 20), "bc");
	BOOST_CHECK_EQUAL(readBytes(inputStream, 20), "");
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace hyperion::util::test
