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

#include <test/TestCase.h>
#include <test/libhyperion/ABIJsonTest.h>
#include <test/libhyperion/ASTJSONTest.h>
#include <test/libhyperion/ASTPropertyTest.h>
#include <test/libhyperion/GasTest.h>
#include <test/libhyperion/MemoryGuardTest.h>
#include <test/libhyperion/NatspecJSONTest.h>
#include <test/libhyperion/SyntaxTest.h>
#include <test/libhyperion/SemanticTest.h>
#include <test/libhyperion/SMTCheckerTest.h>
#include <test/libyul/ControlFlowGraphTest.h>
#include <test/libyul/ZVMCodeTransformTest.h>
#include <test/libyul/YulOptimizerTest.h>
#include <test/libyul/YulInterpreterTest.h>
#include <test/libyul/ObjectCompilerTest.h>
#include <test/libyul/ControlFlowSideEffectsTest.h>
#include <test/libyul/FunctionSideEffects.h>
#include <test/libyul/StackLayoutGeneratorTest.h>
#include <test/libyul/StackShufflingTest.h>
#include <test/libyul/SyntaxTest.h>

#include <boost/filesystem.hpp>

namespace hyperion::frontend::test
{

/** Container for all information regarding a testsuite */
struct Testsuite
{
	char const* title;
	boost::filesystem::path const path;
	boost::filesystem::path const subpath;
	bool smt;
	bool needsVM;
	TestCase::TestCaseCreator testCaseCreator;
	std::vector<std::string> labels{};
};


/// Array of testsuits that can be run interactively as well as automatically
Testsuite const g_interactiveTestsuites[] = {
/*
	Title                   Path           Subpath                SMT   NeedsVM Creator function */
	{"Yul Optimizer",          "libyul",      "yulOptimizerTests",     false, false, &yul::test::YulOptimizerTest::create},
	{"Yul Interpreter",        "libyul",      "yulInterpreterTests",   false, false, &yul::test::YulInterpreterTest::create},
	{"Yul Object Compiler",    "libyul",      "objectCompiler",        false, false, &yul::test::ObjectCompilerTest::create},
	{"Yul Control Flow Graph", "libyul",      "yulControlFlowGraph",   false, false, &yul::test::ControlFlowGraphTest::create},
	{"Yul Stack Layout",       "libyul",      "yulStackLayout",        false, false, &yul::test::StackLayoutGeneratorTest::create},
	{"Yul Stack Shuffling",    "libyul",      "yulStackShuffling",     false, false, &yul::test::StackShufflingTest::create},
	{"Control Flow Side Effects","libyul",    "controlFlowSideEffects",false, false, &yul::test::ControlFlowSideEffectsTest::create},
	{"Function Side Effects",  "libyul",      "functionSideEffects",   false, false, &yul::test::FunctionSideEffects::create},
	{"Yul Syntax",             "libyul",      "yulSyntaxTests",        false, false, &yul::test::SyntaxTest::create},
	{"ZVM Code Transform",     "libyul",      "zvmCodeTransform",      false, false, &yul::test::ZVMCodeTransformTest::create, {"nooptions"}},
	{"Syntax",                 "libhyperion", "syntaxTests",           false, false, &SyntaxTest::create},
	{"Semantic",               "libhyperion", "semanticTests",         false, true,  &SemanticTest::create},
	{"JSON AST",               "libhyperion", "ASTJSON",               false, false, &ASTJSONTest::create},
	{"JSON ABI",               "libhyperion", "ABIJson",               false, false, &ABIJsonTest::create},
	{"JSON Natspec",           "libhyperion", "natspecJSON",           false, false, &NatspecJSONTest::create},
	{"SMT Checker",            "libhyperion", "smtCheckerTests",       true,  false, &SMTCheckerTest::create},
	{"Gas Estimates",          "libhyperion", "gasTests",              false, false, &GasTest::create},
	{"Memory Guard",           "libhyperion", "memoryGuardTests",      false, false, &MemoryGuardTest::create},
	{"AST Properties",         "libhyperion", "astPropertyTests",      false, false, &ASTPropertyTest::create},
};

}
