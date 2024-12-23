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

#include <test/ZVMHost.h>

#include <libhyperion/interface/CompilerStack.h>

#include <libyul/YulStack.h>

#include <libhyputil/Keccak256.h>

#include <zvmone/zvmone.h>

namespace hyperion::test::fuzzer
{
struct CompilerOutput
{
	/// ZVM bytecode returned by compiler
	hyperion::bytes byteCode;
	/// Method identifiers in a contract
	Json::Value methodIdentifiersInContract;
};

struct CompilerInput
{
	CompilerInput(
		langutil::ZVMVersion _zvmVersion,
		StringMap const& _sourceCode,
		std::string const& _contractName,
		frontend::OptimiserSettings _optimiserSettings,
		std::map<std::string, hyperion::util::h160> _libraryAddresses,
		bool _debugFailure = false,
		bool _viaIR = false
	):
		zvmVersion(_zvmVersion),
		sourceCode(_sourceCode),
		contractName(_contractName),
		optimiserSettings(_optimiserSettings),
		libraryAddresses(_libraryAddresses),
		debugFailure(_debugFailure),
		viaIR(_viaIR)
	{}
	/// ZVM target version
	langutil::ZVMVersion zvmVersion;
	/// Source code to be compiled
	StringMap const& sourceCode;
	/// Contract name without a colon prefix
	std::string contractName;
	/// Optimiser setting to be used during compilation
	frontend::OptimiserSettings optimiserSettings;
	/// Information on which library is deployed where
	std::map<std::string, hyperion::util::h160> libraryAddresses;
	/// Flag used for debugging
	bool debugFailure;
	/// Flag to enable new code generator.
	bool viaIR;
};

class HyperionCompilationFramework
{
public:
	HyperionCompilationFramework(CompilerInput _input): m_compilerInput(_input)
	{}
	/// Sets contract name to @param _contractName.
	void contractName(std::string const& _contractName)
	{
		m_compilerInput.contractName = _contractName;
	}
	/// Sets library addresses to @param _libraryAddresses.
	void libraryAddresses(std::map<std::string, hyperion::util::h160> _libraryAddresses)
	{
		m_compilerInput.libraryAddresses = std::move(_libraryAddresses);
	}
	/// @returns method identifiers in contract called @param _contractName.
	Json::Value methodIdentifiers(std::string const& _contractName)
	{
		return m_compiler.interfaceSymbols(_contractName)["methods"];
	}
	/// @returns Compilation output comprising ZVM bytecode and list of
	/// method identifiers in contract if compilation is successful,
	/// null value otherwise.
	std::optional<CompilerOutput> compileContract();
private:
	frontend::CompilerStack m_compiler;
	CompilerInput m_compilerInput;
};

class ZvmoneUtility
{
public:
	ZvmoneUtility(
		hyperion::test::ZVMHost& _zvmHost,
		CompilerInput _compilerInput,
		std::string const& _contractName,
		std::string const& _libraryName,
		std::string const& _methodName
	):
		m_zvmHost(_zvmHost),
		m_compilationFramework(_compilerInput),
		m_contractName(_contractName),
		m_libraryName(_libraryName),
		m_methodName(_methodName)
	{}
	/// @returns the result returned by the ZVM host on compiling, deploying,
	/// and executing test configuration.
	/// @param _isabelleData contains encoding data to be passed to the
	/// isabelle test entry point.
	zvmc::Result compileDeployAndExecute(std::string _isabelleData = {});
	/// Compares the contents of the memory address pointed to
	/// by `_result` of `_length` bytes to u256 zero.
	/// @returns true if `_result` is zero, false
	/// otherwise.
	static bool zeroWord(uint8_t const* _result, size_t _length);
	/// @returns an zvmc_message with all of its fields zero
	/// initialized except gas and input fields.
	/// The gas field is set to the maximum permissible value so that we
	/// don't run into out of gas errors. The input field is copied from
	/// @param _input.
	static zvmc_message initializeMessage(bytes const& _input);
private:
	/// @returns the result of the execution of the function whose
	/// keccak256 hash is @param _functionHash that is deployed at
	/// @param _deployedAddress in @param _hostContext.
	zvmc::Result executeContract(
		bytes const& _functionHash,
		zvmc_address _deployedAddress
	);
	/// @returns the result of deployment of @param _code on @param _hostContext.
	zvmc::Result deployContract(bytes const& _code);
	/// Deploys and executes ZVM byte code in @param _byteCode on
	/// ZVM Host referenced by @param _hostContext. Input passed
	/// to execution context is @param _hexEncodedInput.
	/// @returns result returning by @param _hostContext.
	zvmc::Result deployAndExecute(
		bytes const& _byteCode,
		std::string const& _hexEncodedInput
	);
	/// Compiles contract named @param _contractName present in
	/// @param _sourceCode, optionally using a precompiled library
	/// specified via a library mapping and an optimisation setting.
	/// @returns a pair containing the generated byte code and method
	/// identifiers for methods in @param _contractName.
	std::optional<CompilerOutput> compileContract();

	/// ZVM Host implementation
	hyperion::test::ZVMHost& m_zvmHost;
	/// Hyperion compilation framework
	HyperionCompilationFramework m_compilationFramework;
	/// Contract name
	std::string m_contractName;
	/// Library name
	std::string m_libraryName;
	/// Method name
	std::string m_methodName;
};

}
