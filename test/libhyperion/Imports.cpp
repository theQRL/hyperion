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
 * Tests for high level features like import.
 */

#include <test/libhyperion/ErrorCheck.h>
#include <test/Common.h>

#include <liblangutil/Exceptions.h>
#include <libhyperion/interface/CompilerStack.h>
#include <libhyperion/interface/ImportRemapper.h>

#include <boost/test/unit_test.hpp>

#include <string>


namespace hyperion::frontend::test
{

BOOST_AUTO_TEST_SUITE(HyperionImports)

BOOST_AUTO_TEST_CASE(remappings)
{
	CompilerStack c;
	c.setRemappings(std::vector<ImportRemapper::Remapping>{{"", "s", "s_1.4.6"},{"", "t", "Tee"}});
	c.setSources({
		{"a", "import \"s/s.hyp\"; contract A is S {} pragma hyperion >=0.0;"},
		{"b", "import \"t/tee.hyp\"; contract A is Tee {} pragma hyperion >=0.0;"},
		{"s_1.4.6/s.hyp", "contract S {} pragma hyperion >=0.0;"},
		{"Tee/tee.hyp", "contract Tee {} pragma hyperion >=0.0;"}
	});
	c.setZVMVersion(hyperion::test::CommonOptions::get().zvmVersion());
	BOOST_CHECK(c.compile());
}

BOOST_AUTO_TEST_CASE(context_dependent_remappings)
{
	CompilerStack c;
	c.setRemappings(std::vector<ImportRemapper::Remapping>{{"a", "s", "s_1.4.6"}, {"b", "s", "s_1.4.7"}});
	c.setSources({
		{"a/a.hyp", "import \"s/s.hyp\"; contract A is SSix {} pragma hyperion >=0.0;"},
		{"b/b.hyp", "import \"s/s.hyp\"; contract B is SSeven {} pragma hyperion >=0.0;"},
		{"s_1.4.6/s.hyp", "contract SSix {} pragma hyperion >=0.0;"},
		{"s_1.4.7/s.hyp", "contract SSeven {} pragma hyperion >=0.0;"}
	});
	c.setZVMVersion(hyperion::test::CommonOptions::get().zvmVersion());
	BOOST_CHECK(c.compile());
}

BOOST_AUTO_TEST_CASE(context_dependent_remappings_ensure_default_and_module_preserved)
{
	CompilerStack c;
	c.setRemappings(std::vector<ImportRemapper::Remapping>{
		{"", "foo", "vendor/foo_2.0.0"},
		{"vendor/bar", "foo", "vendor/foo_1.0.0"},
		{"", "bar", "vendor/bar"}
	});
	c.setSources({
		{"main.hyp", "import \"foo/foo.hyp\"; import {Bar} from \"bar/bar.hyp\"; contract Main is Foo2, Bar {} pragma hyperion >=0.0;"},
		{"vendor/bar/bar.hyp", "import \"foo/foo.hyp\"; contract Bar {Foo1 foo;} pragma hyperion >=0.0;"},
		{"vendor/foo_1.0.0/foo.hyp", "contract Foo1 {} pragma hyperion >=0.0;"},
		{"vendor/foo_2.0.0/foo.hyp", "contract Foo2 {} pragma hyperion >=0.0;"}
	});
	c.setZVMVersion(hyperion::test::CommonOptions::get().zvmVersion());
	BOOST_CHECK(c.compile());
}

BOOST_AUTO_TEST_CASE(context_dependent_remappings_order_independent_1)
{
	CompilerStack c;
	c.setRemappings(std::vector<ImportRemapper::Remapping>{{"a", "x/y/z", "d"}, {"a/b", "x", "e"}});
	c.setSources({
		{"a/main.hyp", "import \"x/y/z/z.hyp\"; contract Main is D {} pragma hyperion >=0.0;"},
		{"a/b/main.hyp", "import \"x/y/z/z.hyp\"; contract Main is E {} pragma hyperion >=0.0;"},
		{"d/z.hyp", "contract D {} pragma hyperion >=0.0;"},
		{"e/y/z/z.hyp", "contract E {} pragma hyperion >=0.0;"}
	});
	c.setZVMVersion(hyperion::test::CommonOptions::get().zvmVersion());
	BOOST_CHECK(c.compile());
}

BOOST_AUTO_TEST_CASE(context_dependent_remappings_order_independent_2)
{
	CompilerStack c;
	c.setRemappings(std::vector<ImportRemapper::Remapping>{{"a/b", "x", "e"}, {"a", "x/y/z", "d"}});
	c.setSources({
		{"a/main.hyp", "import \"x/y/z/z.hyp\"; contract Main is D {} pragma hyperion >=0.0;"},
		{"a/b/main.hyp", "import \"x/y/z/z.hyp\"; contract Main is E {} pragma hyperion >=0.0;"},
		{"d/z.hyp", "contract D {} pragma hyperion >=0.0;"},
		{"e/y/z/z.hyp", "contract E {} pragma hyperion >=0.0;"}
	});
	c.setZVMVersion(hyperion::test::CommonOptions::get().zvmVersion());
	BOOST_CHECK(c.compile());
}

BOOST_AUTO_TEST_SUITE_END()

} // end namespaces
