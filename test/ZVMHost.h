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
 * ZVM execution host, i.e. component that implements a simulated Ethereum blockchain
 * for testing purposes.
 */

#pragma once

#include <test/zvmc/mocked_host.hpp>
#include <test/zvmc/zvmc.hpp>
#include <test/zvmc/zvmc.h>

#include <liblangutil/ZVMVersion.h>

#include <libhyputil/FixedHash.h>

#include <boost/filesystem.hpp>

namespace hyperion::test
{
using Address = util::h160;
using StorageMap = std::map<zvmc::bytes32, zvmc::StorageValue>;

struct ZVMPrecompileOutput {
	bytes const output;
	int64_t gas_used;
};

class ZVMHost: public zvmc::MockedHost
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
	zvmc::Result call(zvmc_message const& _message) noexcept final;
	zvmc::bytes32 get_block_hash(int64_t number) const noexcept final;

	// Hyperion testing specific features.

	/// Tries to dynamically load an zvmc vm supporting zvm1 and caches the loaded VM.
	/// @returns vmc::VM(nullptr) on failure.
	static zvmc::VM& getVM(std::string const& _path = {});

	/// Tries to load all defined zvmc vm shared libraries.
	/// @param _vmPaths paths to multiple zvmc shared libraries.
	/// @throw Exception if multiple zvm1 vms where loaded.
	/// @returns true, if an zvmc vm supporting zvm1 was loaded properly,
	static bool checkVmPaths(std::vector<boost::filesystem::path> const& _vmPaths);

	explicit ZVMHost(langutil::ZVMVersion _zvmVersion, zvmc::VM& _vm);

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
	StorageMap const& get_address_storage(zvmc::address const& _addr);

	static Address convertFromZVMC(zvmc::address const& _addr);
	static zvmc::address convertToZVMC(Address const& _addr);
	static util::h256 convertFromZVMC(zvmc::bytes32 const& _data);
	static zvmc::bytes32 convertToZVMC(util::h256 const& _data);
private:
	/// Transfer value between accounts. Checks for sufficient balance.
	void transfer(zvmc::MockedAccount& _sender, zvmc::MockedAccount& _recipient, u256 const& _value) noexcept;

	/// Start a new transaction frame.
	/// This will apply storage status changes across all accounts,
	/// and clear account/storage access indicator for EIP-2929.
	void newTransactionFrame();

	/// Records calls made via @param _message.
	void recordCalls(zvmc_message const& _message) noexcept;

	static zvmc::Result precompileDepositRoot(zvmc_message const& _message) noexcept;
	static zvmc::Result precompileSha256(zvmc_message const& _message) noexcept;
	static zvmc::Result precompileIdentity(zvmc_message const& _message) noexcept;
	static zvmc::Result precompileModExp(zvmc_message const& _message) noexcept;
	static zvmc::Result precompileALTBN128G1Add(zvmc_message const& _message) noexcept;
	static zvmc::Result precompileALTBN128G1Mul(zvmc_message const& _message) noexcept;
	static zvmc::Result precompileALTBN128PairingProduct(zvmc_message const& _message) noexcept;
	static zvmc::Result precompileGeneric(zvmc_message const& _message, std::map<bytes, ZVMPrecompileOutput> const& _inOut) noexcept;
	/// @returns a result object with gas usage and result data taken from @a _data.
	/// The outcome will be a failure if the limit < required.
	/// @note The return value is only valid as long as @a _data is alive!
	static zvmc::Result resultWithGas(int64_t gas_limit, int64_t gas_required, bytes const& _data) noexcept;
	static zvmc::Result resultWithFailure() noexcept;

	zvmc::VM& m_vm;
	/// ZVM version requested by the testing tool
	langutil::ZVMVersion m_zvmVersion;
	/// ZVM version requested from ZVMC (matches the above)
	zvmc_revision m_zvmRevision;
};

class ZVMHostPrinter
{
public:
	/// Constructs a host printer object for state at @param _address.
	explicit ZVMHostPrinter(ZVMHost& _host, zvmc::address _address):
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
	ZVMHost& m_host;
	zvmc::address m_account;
};

}
