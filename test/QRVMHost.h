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
 * QRVM execution host, i.e. component that implements a simulated QRL blockchain
 * for testing purposes.
 */

#pragma once

#include <test/qrvmc/mocked_host.hpp>
#include <test/qrvmc/qrvmc.hpp>
#include <test/qrvmc/qrvmc.h>

#include <liblangutil/QRVMVersion.h>

#include <libhyputil/FixedHash.h>

#include <boost/filesystem.hpp>

namespace hyperion::test
{
using Address = util::h160;
using StorageMap = std::map<qrvmc::bytes32, qrvmc::StorageValue>;

struct QRVMPrecompileOutput {
	bytes const output;
	int64_t gas_used;
};

class QRVMHost: public qrvmc::MockedHost
{
public:
	// Verbatim features of MockedHost.
	using MockedHost::account_exists;
	using MockedHost::get_storage;
	using MockedHost::set_storage;
	using MockedHost::get_balance;
	using MockedHost::get_code_size;
	using MockedHost::get_code_hash;
	using MockedHost::copy_code;
	using MockedHost::get_tx_context;
	using MockedHost::emit_log;
	using MockedHost::access_account;
	using MockedHost::access_storage;

	// Modified features of MockedHost.
	qrvmc::Result call(qrvmc_message const& _message) noexcept final;
	qrvmc::bytes32 get_block_hash(int64_t number) const noexcept final;

	// Hyperion testing specific features.

	/// Tries to dynamically load a qrvmc vm supporting qrvm1 and caches the loaded VM.
	/// @returns vmc::VM(nullptr) on failure.
	static qrvmc::VM& getVM(std::string const& _path = {});

	/// Tries to load all defined qrvmc vm shared libraries.
	/// @param _vmPaths paths to multiple qrvmc shared libraries.
	/// @throw Exception if multiple qrvm1 vms where loaded.
	/// @returns true, if a qrvmc vm supporting qrvm1 was loaded properly,
	static bool checkVmPaths(std::vector<boost::filesystem::path> const& _vmPaths);

	explicit QRVMHost(langutil::QRVMVersion _qrvmVersion, qrvmc::VM& _vm);

	/// Reset entire state (including accounts).
	void reset();

	/// Start new block.
	void newBlock()
	{
		tx_context.block_number++;
		tx_context.block_timestamp += 15;
		recorded_logs.clear();
		newTransactionFrame();
	}

	/// @returns contents of storage at @param _addr.
	StorageMap const& get_address_storage(qrvmc::address const& _addr);

	static Address convertFromQRVMC(qrvmc::address const& _addr);
	static qrvmc::address convertToQRVMC(Address const& _addr);
	static util::h256 convertFromQRVMC(qrvmc::bytes32 const& _data);
	static qrvmc::bytes32 convertToQRVMC(util::h256 const& _data);
private:
	/// Transfer value between accounts. Checks for sufficient balance.
	void transfer(qrvmc::MockedAccount& _sender, qrvmc::MockedAccount& _recipient, u256 const& _value) noexcept;

	/// Start a new transaction frame.
	/// This will apply storage status changes across all accounts,
	/// and clear account/storage access indicator for EIP-2929.
	void newTransactionFrame();

	/// Records calls made via @param _message.
	void recordCalls(qrvmc_message const& _message) noexcept;

	static qrvmc::Result precompileDepositRoot(qrvmc_message const& _message) noexcept;
	static qrvmc::Result precompileSha256(qrvmc_message const& _message) noexcept;
	static qrvmc::Result precompileIdentity(qrvmc_message const& _message) noexcept;
	static qrvmc::Result precompileModExp(qrvmc_message const& _message) noexcept;
	static qrvmc::Result precompileGeneric(qrvmc_message const& _message, std::map<bytes, QRVMPrecompileOutput> const& _inOut) noexcept;
	/// @returns a result object with gas usage and result data taken from @a _data.
	/// The outcome will be a failure if the limit < required.
	/// @note The return value is only valid as long as @a _data is alive!
	static qrvmc::Result resultWithGas(int64_t gas_limit, int64_t gas_required, bytes const& _data) noexcept;
	static qrvmc::Result resultWithFailure() noexcept;

	qrvmc::VM& m_vm;
	/// QRVM version requested by the testing tool
	langutil::QRVMVersion m_qrvmVersion;
	/// QRVM version requested from QRVMC (matches the above)
	qrvmc_revision m_qrvmRevision;
};

class QRVMHostPrinter
{
public:
	/// Constructs a host printer object for state at @param _address.
	explicit QRVMHostPrinter(QRVMHost& _host, qrvmc::address _address):
		m_host(_host),
		m_account(_address)
	{}
	/// @returns state at account maintained by host.
	std::string state();
private:
	/// Outputs storage at account to stateStream.
	void storage();
	/// Outputs call records for account to stateStream.
	void callRecords();
	/// Outputs balance of account to stateStream.
	void balance();

	std::ostringstream m_stateStream;
	QRVMHost& m_host;
	qrvmc::address m_account;
};

}
