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

#include <test/tools/ossfuzz/HyperionQrvmoneInterface.h>

#include <liblangutil/Exceptions.h>
#include <liblangutil/SourceReferenceFormatter.h>

#include <range/v3/algorithm/all_of.hpp>
#include <range/v3/span.hpp>

using namespace hyperion::test::fuzzer;
using namespace hyperion::frontend;
using namespace hyperion::langutil;
using namespace hyperion::util;
using namespace std;

optional<CompilerOutput> HyperionCompilationFramework::compileContract()
{
	m_compiler.setSources(m_compilerInput.sourceCode);
	m_compiler.setLibraries(m_compilerInput.libraryAddresses);
	m_compiler.setQRVMVersion(m_compilerInput.qrvmVersion);
	m_compiler.setOptimiserSettings(m_compilerInput.optimiserSettings);
	m_compiler.setViaIR(m_compilerInput.viaIR);
	if (!m_compiler.compile())
	{
		if (m_compilerInput.debugFailure)
		{
			cerr << "Compiling contract failed" << endl;
			cerr << SourceReferenceFormatter::formatErrorInformation(
				m_compiler.errors(),
				m_compiler
			);
		}
		return {};
	}
	else
	{
		string contractName;
		if (m_compilerInput.contractName.empty())
			contractName = m_compiler.lastContractName();
		else
			contractName = m_compilerInput.contractName;
		qrvmasm::LinkerObject obj = m_compiler.object(contractName);
		Json::Value methodIdentifiers = m_compiler.interfaceSymbols(contractName)["methods"];
		return CompilerOutput{obj.bytecode, methodIdentifiers};
	}
}

bool QrvmoneUtility::zeroWord(uint8_t const* _result, size_t _length)
{
	return _length == 32 &&
		ranges::all_of(
			ranges::span(_result, static_cast<long>(_length)),
			[](uint8_t _v) { return _v == 0; });
}

qrvmc_message QrvmoneUtility::initializeMessage(bytes const& _input)
{
	// Zero initialize all message fields
	qrvmc_message msg = {};
	// Gas available (value of type int64_t) is set to its maximum
	// value.
	msg.gas = std::numeric_limits<int64_t>::max();
	msg.input_data = _input.data();
	msg.input_size = _input.size();
	return msg;
}

qrvmc::Result QrvmoneUtility::executeContract(
	bytes const& _functionHash,
	qrvmc_address _deployedAddress
)
{
	qrvmc_message message = initializeMessage(_functionHash);
	message.recipient = _deployedAddress;
	message.code_address = _deployedAddress;
	message.kind = QRVMC_CALL;
	return m_qrvmHost.call(message);
}

qrvmc::Result QrvmoneUtility::deployContract(bytes const& _code)
{
	qrvmc_message message = initializeMessage(_code);
	message.kind = QRVMC_CREATE;
	return m_qrvmHost.call(message);
}

qrvmc::Result QrvmoneUtility::deployAndExecute(
	bytes const& _byteCode,
	string const& _hexEncodedInput
)
{
	// Deploy contract and signal failure if deploy failed
	qrvmc::Result createResult = deployContract(_byteCode);
	hypAssert(
		createResult.status_code == QRVMC_SUCCESS,
		"HyperionQrvmoneInterface: Contract creation failed"
	);

	// Execute test function and signal failure if QRVM reverted or
	// did not return expected output on successful execution.
	qrvmc::Result callResult = executeContract(
		util::fromHex(_hexEncodedInput),
		createResult.create_address
	);

	// We don't care about QRVM One failures other than QRVMC_REVERT
	hypAssert(
		callResult.status_code != QRVMC_REVERT,
		"HyperionQrvmoneInterface: QRVM One reverted"
	);
	return callResult;
}

qrvmc::Result QrvmoneUtility::compileDeployAndExecute(string _fuzzIsabelle)
{
	map<string, h160> libraryAddressMap;
	// Stage 1: Compile and deploy library if present.
	if (!m_libraryName.empty())
	{
		m_compilationFramework.contractName(m_libraryName);
		auto compilationOutput = m_compilationFramework.compileContract();
		hypAssert(compilationOutput.has_value(), "Compiling library failed");
		CompilerOutput cOutput = compilationOutput.value();
		// Deploy contract and signal failure if deploy failed
		qrvmc::Result createResult = deployContract(cOutput.byteCode);
		hypAssert(
			createResult.status_code == QRVMC_SUCCESS,
			"HyperionQrvmoneInterface: Library deployment failed"
		);
		libraryAddressMap[m_libraryName] = QRVMHost::convertFromQRVMC(createResult.create_address);
		m_compilationFramework.libraryAddresses(libraryAddressMap);
	}

	// Stage 2: Compile, deploy, and execute contract, optionally using library
	// address map.
	m_compilationFramework.contractName(m_contractName);
	auto cOutput = m_compilationFramework.compileContract();
	hypAssert(cOutput.has_value(), "Compiling contract failed");
	hypAssert(
		!cOutput->byteCode.empty() && !cOutput->methodIdentifiersInContract.empty(),
		"HyperionQrvmoneInterface: Invalid compilation output."
	);

	string methodName;
	if (!_fuzzIsabelle.empty())
		// TODO: Remove this once a cleaner solution is found for querying
		// isabelle test entry point. At the moment, we are sure that the
		// entry point is the second method in the contract (hence the ++)
		// but not its name.
		methodName = (++cOutput->methodIdentifiersInContract.begin())->asString() +
			_fuzzIsabelle.substr(2, _fuzzIsabelle.size());
	else
		methodName = cOutput->methodIdentifiersInContract[m_methodName].asString();

	return deployAndExecute(
		cOutput->byteCode,
		methodName
	);
}

optional<CompilerOutput> QrvmoneUtility::compileContract()
{
	try
	{
		return m_compilationFramework.compileContract();
	}
	catch (qrvmasm::StackTooDeepException const&)
	{
		return {};
	}
}
