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

#include <test/QRVMHost.h>

#include <test/qrvmc/loader.h>

#include <libqrvmasm/GasMeter.h>

#include <libhyputil/Exceptions.h>
#include <libhyputil/Assertions.h>
#include <libhyputil/Keccak256.h>
#include <libhyputil/picosha2.h>

using namespace std;
using namespace hyperion;
using namespace hyperion::util;
using namespace hyperion::test;
using namespace qrvmc::literals;

qrvmc::VM& QRVMHost::getVM(string const& _path)
{
	static qrvmc::VM NullVM{nullptr};
	static map<string, unique_ptr<qrvmc::VM>> vms;
	if (vms.count(_path) == 0)
	{
		qrvmc_loader_error_code errorCode = {};
		auto vm = qrvmc::VM{qrvmc_load_and_configure(_path.c_str(), &errorCode)};
		if (vm && errorCode == QRVMC_LOADER_SUCCESS)
		{
			if (vm.get_capabilities() & (QRVMC_CAPABILITY_QRVM1))
				vms[_path] = make_unique<qrvmc::VM>(qrvmc::VM(std::move(vm)));
			else
				cerr << "VM loaded does not support QRVM1" << endl;
		}
		else
		{
			cerr << "Error loading VM from " << _path;
			if (char const* errorMsg = qrvmc_last_error_msg())
				cerr << ":" << endl << errorMsg;
			cerr << endl;
		}
	}

	if (vms.count(_path) > 0)
		return *vms[_path];

	return NullVM;
}

bool QRVMHost::checkVmPaths(vector<boost::filesystem::path> const& _vmPaths)
{
	bool qrvmVmFound = false;
	for (auto const& path: _vmPaths)
	{
		qrvmc::VM& vm = QRVMHost::getVM(path.string());
		if (!vm)
			continue;

		if (vm.has_capability(QRVMC_CAPABILITY_QRVM1))
		{
			if (qrvmVmFound)
				BOOST_THROW_EXCEPTION(runtime_error("Multiple qrvm1 qrvmc vms defined. Please only define one qrvm1 qrvmc vm."));
			qrvmVmFound = true;
		}
	}
	return qrvmVmFound;
}

QRVMHost::QRVMHost(langutil::QRVMVersion _qrvmVersion, qrvmc::VM& _vm):
	m_vm(_vm),
	m_qrvmVersion(_qrvmVersion)
{
	if (!m_vm)
	{
		cerr << "Unable to find qrvmone library" << endl;
		assertThrow(false, Exception, "");
	}

	if (_qrvmVersion == langutil::QRVMVersion::zond())
		m_qrvmRevision = QRVMC_ZOND;
	else
		assertThrow(false, Exception, "Unsupported QRVM version");

	
	// This is the value from the merge block.
	tx_context.block_prev_randao = 0xa86c2e601b6c44eb4848f7d23d9df3113fbcac42041c49cbed5000cb4f118777_bytes32;
	tx_context.block_gas_limit = 20000000;
	tx_context.block_coinbase = "Q7878787878787878787878787878787878787878"_address;
	tx_context.tx_gas_price = qrvmc::uint256be{3000000000};
	tx_context.tx_origin = "Q9292929292929292929292929292929292929292"_address;
	// Mainnet according to EIP-155
	tx_context.chain_id = qrvmc::uint256be{1};
	// The minimum value of basefee
	tx_context.block_base_fee = qrvmc::bytes32{7};

	// Reserve space for recording calls.
	if (!recorded_calls.capacity())
		recorded_calls.reserve(max_recorded_calls);

	reset();
}

void QRVMHost::reset()
{
	accounts.clear();
	// Clear call records
	recorded_calls.clear();
	// Clear EIP-2929 account access indicator
	recorded_account_accesses.clear();

	// Mark all precompiled contracts as existing. Existing here means to have a balance (as per EIP-161).
	// NOTE: keep this in sync with `QRVMHost::call` below.
	//
	// A lot of precompile addresses had a balance before they became valid addresses for precompiles.
	// For example all the precompile addresses allocated in Byzantium had a 1 planck balance sent to them
	// roughly 22 days before the update went live.
	for (unsigned precompiledAddress = 1; precompiledAddress <= 8; precompiledAddress++)
	{
		qrvmc::address address{precompiledAddress};
		// 1planck
		accounts[address].balance = qrvmc::uint256be{1};
		// Set according to EIP-1052.
		accounts[address].codehash = 0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470_bytes32;		
	}
}

void QRVMHost::newTransactionFrame()
{
	// Clear EIP-2929 account access indicator
	recorded_account_accesses.clear();

	for (auto& [address, account]: accounts)
		for (auto& [slot, value]: account.storage)
		{
			value.access_status = QRVMC_ACCESS_COLD; // Clear EIP-2929 storage access indicator
			value.original = value.current;			// Clear EIP-2200 dirty slot
		}
}

void QRVMHost::transfer(qrvmc::MockedAccount& _sender, qrvmc::MockedAccount& _recipient, u256 const& _value) noexcept
{
	assertThrow(u256(convertFromQRVMC(_sender.balance)) >= _value, Exception, "Insufficient balance for transfer");
	_sender.balance = convertToQRVMC(u256(convertFromQRVMC(_sender.balance)) - _value);
	_recipient.balance = convertToQRVMC(u256(convertFromQRVMC(_recipient.balance)) + _value);
}

void QRVMHost::recordCalls(qrvmc_message const& _message) noexcept
{
	if (recorded_calls.size() < max_recorded_calls)
		recorded_calls.emplace_back(_message);
}

// NOTE: this is used for both internal and external calls.
// External calls are triggered from ExecutionFramework and contain only QRVMC_CREATE or QRVMC_CALL.
qrvmc::Result QRVMHost::call(qrvmc_message const& _message) noexcept
{
	recordCalls(_message);
	if (_message.recipient == "Q0000000000000000000000000000000000000001"_address)
		return precompileDepositRoot(_message);
	else if (_message.recipient == "Q0000000000000000000000000000000000000002"_address)
		return precompileSha256(_message);
	else if (_message.recipient == "Q0000000000000000000000000000000000000004"_address)
		return precompileIdentity(_message);
	else if (_message.recipient == "Q0000000000000000000000000000000000000005"_address)
		return precompileModExp(_message);

	auto const stateBackup = accounts;

	u256 value{convertFromQRVMC(_message.value)};
	auto& sender = accounts[_message.sender];

	qrvmc::bytes code;

	qrvmc_message message = _message;
	if (message.depth == 0)
	{
		message.gas -= message.kind == QRVMC_CREATE ? qrvmasm::GasCosts::txCreateGas : qrvmasm::GasCosts::txGas;
		for (size_t i = 0; i < message.input_size; ++i)
			message.gas -= message.input_data[i] == 0 ? qrvmasm::GasCosts::txDataZeroGas : qrvmasm::GasCosts::txDataNonZeroGas;
		if (message.gas < 0)
		{
			qrvmc::Result result;
			result.status_code = QRVMC_OUT_OF_GAS;
			accounts = stateBackup;
			return result;
		}
	}

	if (message.kind == QRVMC_CREATE)
	{
		// TODO is the nonce incremented on failure, too?
		// NOTE: nonce for creation from contracts starts at 1
		// TODO: check if sender is an EOA and do not pre-increment
		sender.nonce++;

		auto encodeRlpInteger = [](int value) -> bytes {
			if (value == 0) {
				return bytes{128};
			} else if (value <= 127) {
				return bytes{static_cast<uint8_t>(value)};
			} else if (value <= 0xff) {
				return bytes{128 + 1, static_cast<uint8_t>(value)};
			} else if (value <= 0xffff) {
				return bytes{128 + 55 + 2, static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
			} else {
				assertThrow(false, Exception, "Can only encode RLP numbers <= 0xffff");
			}
		};

		bytes encodedNonce = encodeRlpInteger(sender.nonce);

		h160 createAddress(keccak256(
			bytes{static_cast<uint8_t>(0xc0 + 21 + encodedNonce.size())} +
			bytes{0x94} +
			bytes(begin(message.sender.bytes), end(message.sender.bytes)) +
			encodedNonce
		), h160::AlignRight);

		message.recipient = convertToQRVMC(createAddress);
		assertThrow(accounts.count(message.recipient) == 0, Exception, "Account cannot exist");

		code = qrvmc::bytes(message.input_data, message.input_data + message.input_size);
	}
	else if (message.kind == QRVMC_CREATE2)
	{
		h160 createAddress(keccak256(
			bytes{0xff} +
			bytes(begin(message.sender.bytes), end(message.sender.bytes)) +
			bytes(begin(message.create2_salt.bytes), end(message.create2_salt.bytes)) +
			keccak256(bytes(message.input_data, message.input_data + message.input_size)).asBytes()
		), h160::AlignRight);

		message.recipient = convertToQRVMC(createAddress);
		if (accounts.count(message.recipient) && (
			accounts[message.recipient].nonce > 0 ||
			!accounts[message.recipient].code.empty()
		))
		{
			qrvmc::Result result;
			result.status_code = QRVMC_OUT_OF_GAS;
			accounts = stateBackup;
			return result;
		}

		code = qrvmc::bytes(message.input_data, message.input_data + message.input_size);
	}
	else
		code = accounts[message.code_address].code;

	auto& destination = accounts[message.recipient];

	if (value != 0 && message.kind != QRVMC_DELEGATECALL)
	{
		if (value > convertFromQRVMC(sender.balance))
		{
			qrvmc::Result result;
			result.status_code = QRVMC_INSUFFICIENT_BALANCE;
			accounts = stateBackup;
			return result;
		}
		transfer(sender, destination, value);
	}

	// Populate the access access list.
	// Note, this will also properly touch the created address.
	// TODO: support a user supplied access list too
	access_account(message.sender);
	access_account(message.recipient);
	// EIP-3651 rule
	access_account(tx_context.block_coinbase);

	if (message.kind == QRVMC_CREATE || message.kind == QRVMC_CREATE2)
	{
		message.input_data = nullptr;
		message.input_size = 0;
	}
	qrvmc::Result result = m_vm.execute(*this, m_qrvmRevision, message, code.data(), code.size());

	if (message.kind == QRVMC_CREATE || message.kind == QRVMC_CREATE2)
	{
		result.gas_left -= static_cast<int64_t>(qrvmasm::GasCosts::createDataGas * result.output_size);
		if (result.gas_left < 0)
		{
			result.gas_left = 0;
			result.status_code = QRVMC_OUT_OF_GAS;
			// TODO clear some fields?
		}
		else
		{
			result.create_address = message.recipient;
			destination.code = qrvmc::bytes(result.output_data, result.output_data + result.output_size);
			destination.codehash = convertToQRVMC(keccak256({result.output_data, result.output_size}));
		}
	}

	if (result.status_code != QRVMC_SUCCESS)
		accounts = stateBackup;

	return result;
}

qrvmc::bytes32 QRVMHost::get_block_hash(int64_t _number) const noexcept
{
	return convertToQRVMC(u256("0x3737373737373737373737373737373737373737373737373737373737373737") + _number);
}

h160 QRVMHost::convertFromQRVMC(qrvmc::address const& _addr)
{
	return h160(bytes(begin(_addr.bytes), end(_addr.bytes)));
}

qrvmc::address QRVMHost::convertToQRVMC(h160 const& _addr)
{
	qrvmc::address a;
	for (unsigned i = 0; i < 20; ++i)
		a.bytes[i] = _addr[i];
	return a;
}

h256 QRVMHost::convertFromQRVMC(qrvmc::bytes32 const& _data)
{
	return h256(bytes(begin(_data.bytes), end(_data.bytes)));
}

qrvmc::bytes32 QRVMHost::convertToQRVMC(h256 const& _data)
{
	qrvmc::bytes32 d;
	for (unsigned i = 0; i < 32; ++i)
		d.bytes[i] = _data[i];
	return d;
}

qrvmc::Result QRVMHost::precompileDepositRoot(qrvmc_message const& /*_message*/) noexcept
{
	// TODO implement
	return resultWithFailure();
}

qrvmc::Result QRVMHost::precompileSha256(qrvmc_message const& _message) noexcept
{
	// static data so that we do not need a release routine...
	bytes static hash;
	hash = picosha2::hash256(bytes(
		_message.input_data,
		_message.input_data + _message.input_size
	));

	// Base 60 gas + 12 gas / word.
	int64_t gas_cost = 60 + 12 * ((static_cast<int64_t>(_message.input_size) + 31) / 32);

	return resultWithGas(_message.gas, gas_cost, hash);
}

qrvmc::Result QRVMHost::precompileIdentity(qrvmc_message const& _message) noexcept
{
	// static data so that we do not need a release routine...
	bytes static data;
	data = bytes(_message.input_data, _message.input_data + _message.input_size);

	// Base 15 gas + 3 gas / word.
	int64_t gas_cost = 15 + 3 * ((static_cast<int64_t>(_message.input_size) + 31) / 32);

	return resultWithGas(_message.gas, gas_cost, data);
}

qrvmc::Result QRVMHost::precompileModExp(qrvmc_message const&) noexcept
{
	// TODO implement
	return resultWithFailure();
}

qrvmc::Result QRVMHost::precompileGeneric(
	qrvmc_message const& _message,
	map<bytes, QRVMPrecompileOutput> const& _inOut) noexcept
{
	bytes input(_message.input_data, _message.input_data + _message.input_size);
	if (_inOut.count(input))
	{
		auto const& ret = _inOut.at(input);
		return resultWithGas(_message.gas, ret.gas_used, ret.output);
	}
	else
		return resultWithFailure();
}

qrvmc::Result QRVMHost::resultWithFailure() noexcept
{
	qrvmc::Result result;
	result.status_code = QRVMC_FAILURE;
	return result;
}

qrvmc::Result QRVMHost::resultWithGas(
	int64_t gas_limit,
	int64_t gas_required,
	bytes const& _data
) noexcept
{
	qrvmc::Result result;
	if (gas_limit < gas_required)
	{
		result.status_code = QRVMC_OUT_OF_GAS;
		result.gas_left = 0;
	}
	else
	{
		result.status_code = QRVMC_SUCCESS;
		result.gas_left = gas_limit - gas_required;
	}
	result.output_data = _data.empty() ? nullptr : _data.data();
	result.output_size = _data.size();
	return result;
}

StorageMap const& QRVMHost::get_address_storage(qrvmc::address const& _addr)
{
	assertThrow(account_exists(_addr), Exception, "Account does not exist.");
	return accounts[_addr].storage;
}

string QRVMHostPrinter::state()
{
	// Print state and execution trace.
	if (m_host.account_exists(m_account))
	{
		storage();
		balance();
	}

	callRecords();
	return m_stateStream.str();
}

void QRVMHostPrinter::storage()
{
	for (auto const& [slot, value]: m_host.get_address_storage(m_account))
		if (m_host.get_storage(m_account, slot))
			m_stateStream << "  "
				<< m_host.convertFromQRVMC(slot)
				<< ": "
				<< m_host.convertFromQRVMC(value.current)
				<< endl;
}

void QRVMHostPrinter::balance()
{
	m_stateStream << "BALANCE "
		<< m_host.convertFromQRVMC(m_host.get_balance(m_account))
		<< endl;
}

void QRVMHostPrinter::callRecords()
{
	static auto constexpr callKind = [](qrvmc_call_kind _kind) -> string
	{
		switch (_kind)
		{
			case qrvmc_call_kind::QRVMC_CALL:
				return "CALL";
			case qrvmc_call_kind::QRVMC_DELEGATECALL:
				return "DELEGATECALL";
			case qrvmc_call_kind::QRVMC_CREATE:
				return "CREATE";
			case qrvmc_call_kind::QRVMC_CREATE2:
				return "CREATE2";
			default:
				assertThrow(false, Exception, "Invalid call kind.");
		}
	};

	for (auto const& record: m_host.recorded_calls)
		m_stateStream << callKind(record.kind)
			<< " VALUE "
			<< m_host.convertFromQRVMC(record.value)
			<< endl;
}
