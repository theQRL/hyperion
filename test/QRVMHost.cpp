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
#include <libhyputil/Keccak512.h>
#include <libhyputil/picosha2.h>
#include <libhyputil/XmssVerify.h>

#include <mldsa_native.h>

#include <algorithm>
#include <limits>
#include <vector>

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
	tx_context.block_prev_randao = 0xa86c2e601b6c44eb4848f7d23d9df3113fbcac42041c49cbed5000cb4f118777_bytes64;
	tx_context.block_gas_limit = 20000000;
	tx_context.block_coinbase = "Q00000000000000000000000000000000000000000000000000000000000000000000000000000000000000007878787878787878787878787878787878787878"_address;
	tx_context.tx_gas_price = qrvmc::uint512be{3000000000};
	tx_context.tx_origin = "Q00000000000000000000000000000000000000000000000000000000000000000000000000000000000000009292929292929292929292929292929292929292"_address;
	// Mainnet according to EIP-155
	tx_context.chain_id = qrvmc::uint512be{1};
	// The minimum value of basefee
	tx_context.block_base_fee = qrvmc::bytes64{7};

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
	for (unsigned precompiledAddress = 1; precompiledAddress <= 7; ++precompiledAddress)
	{
		qrvmc::address address{precompiledAddress};
		// 1planck
		accounts[address].balance = qrvmc::uint512be{1};
		// Set according to EIP-1052.
		accounts[address].codehash = 0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470_bytes64;
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

void QRVMHost::transfer(qrvmc::MockedAccount& _sender, qrvmc::MockedAccount& _recipient, u512 const& _value) noexcept
{
	assertThrow(convertUintFromQRVMC(_sender.balance) >= _value, Exception, "Insufficient balance for transfer");
	_sender.balance = convertUintToQRVMC(convertUintFromQRVMC(_sender.balance) - _value);
	_recipient.balance = convertUintToQRVMC(convertUintFromQRVMC(_recipient.balance) + _value);
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
	if (_message.recipient == "Q00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001"_address)
		return precompileDepositRoot(_message);
	else if (_message.recipient == "Q00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000002"_address)
		return precompileSha256(_message);
	else if (_message.recipient == "Q00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000003"_address)
		return precompileMLDSA87Verify(_message);
	else if (_message.recipient == "Q00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000004"_address)
		return precompileIdentity(_message);
	else if (_message.recipient == "Q00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000005"_address)
		return precompileModExp(_message);
	else if (_message.recipient == "Q00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000007"_address)
		return precompileXmssVerify(_message);
	else if (_message.recipient == "Q00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000006"_address)
		return precompileShake256(_message);

	auto stateBackup = accounts;

	u512 value{convertUintFromQRVMC(_message.value)};
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
		bytes encodedNonce = toCompactBigEndian(sender.nonce);
		if (sender.nonce == 0 || sender.nonce >= 0x80)
			encodedNonce = bytes{static_cast<uint8_t>(0x80 + encodedNonce.size())} + encodedNonce;

		// RLP([sender64, nonce]): both the list and the 64-byte sender use long-form lengths.
		h512 createAddress = keccak512(
			asBytes("QRL-ADDR-v1") +
			bytes{0xf8, static_cast<uint8_t>(2 + AddressBytes + encodedNonce.size()), 0xb8, AddressBytes} +
			bytes(begin(message.sender.bytes), end(message.sender.bytes)) +
			encodedNonce
		);

		message.recipient = convertToQRVMC(createAddress);
	}
	else if (message.kind == QRVMC_CREATE2)
	{
		h512 createAddress = keccak512(
			asBytes("QRL-ADDR-v1") +
			bytes{0xff} +
			bytes(begin(message.sender.bytes), end(message.sender.bytes)) +
			bytes(begin(message.create2_salt.bytes), end(message.create2_salt.bytes)) +
			keccak256(bytes(message.input_data, message.input_data + message.input_size)).asBytes()
		);

		message.recipient = convertToQRVMC(createAddress);
	}

	if (message.kind == QRVMC_CREATE || message.kind == QRVMC_CREATE2)
	{
		if (value > convertUintFromQRVMC(sender.balance) || sender.nonce == numeric_limits<uint64_t>::max())
		{
			qrvmc::Result result;
			result.status_code = value > convertUintFromQRVMC(sender.balance) ? QRVMC_INSUFFICIENT_BALANCE : QRVMC_FAILURE;
			result.gas_left = message.gas;
			accounts = stateBackup;
			return result;
		}
		// Both creation opcodes consume the current nonce, even if initialization or collision checks fail.
		++sender.nonce;
		stateBackup[message.sender].nonce = sender.nonce;

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
	if (message.kind == QRVMC_CREATE || message.kind == QRVMC_CREATE2)
		destination.nonce = 1;

	if (value != 0 && message.kind != QRVMC_DELEGATECALL)
	{
		if (value > convertUintFromQRVMC(sender.balance))
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
			destination.codehash = convertToQRVMC(
				keccak256({result.output_data, result.output_size})
			);
		}
	}

	if (result.status_code != QRVMC_SUCCESS)
		accounts = stateBackup;

	return result;
}

qrvmc::bytes64 QRVMHost::get_block_hash(int64_t _number) const noexcept
{
	int64_t const currentBlock = tx_context.block_number;
	if (_number < 0 || _number >= currentBlock || _number < currentBlock - 256)
		return {};
	return convertUintToQRVMC(u256("0x3737373737373737373737373737373737373737373737373737373737373737") + _number);
}

h512 QRVMHost::convertFromQRVMC(qrvmc::address const& _addr)
{
	return h512(bytes(begin(_addr.bytes), end(_addr.bytes)));
}

qrvmc::address QRVMHost::convertToQRVMC(h512 const& _addr)
{
	qrvmc::address a;
	for (unsigned i = 0; i < AddressBytes; ++i)
		a.bytes[i] = _addr[i];
	return a;
}

h512 QRVMHost::convertFromQRVMC(qrvmc::bytes64 const& _data)
{
	return h512(bytes(begin(_data.bytes), end(_data.bytes)));
}

qrvmc::bytes64 QRVMHost::convertToQRVMC(h256 const& _data)
{
	qrvmc::bytes64 d{};
	// bytes32/hash left-aligned: place in upper 32 bytes (bytes 0-31).
	for (unsigned i = 0; i < 32; ++i)
		d.bytes[i] = _data[i];
	return d;
}

u512 QRVMHost::convertUintFromQRVMC(qrvmc::bytes64 const& _data)
{
	u512 result = 0;
	for (unsigned i = 0; i < 64; ++i)
		result = (result << 8) | _data.bytes[i];
	return result;
}

qrvmc::bytes64 QRVMHost::convertUintToQRVMC(u512 const& _value)
{
	qrvmc::bytes64 d{};
	bytes be = toBigEndian(_value);
	for (unsigned i = 0; i < 64; ++i)
		d.bytes[i] = be[i];
	return d;
}

namespace
{

/// SSZ merkleization helpers for the depositroot precompile (mirrors the
/// beacon chain's DepositData hash tree root, see go-qrl core/vm/contracts.go).
bytes sszHashPair(bytes const& _left, bytes const& _right)
{
	bytes joined = _left;
	joined += _right;
	return picosha2::hash256(joined);
}

/// Splits @a _data into 32-byte chunks (zero-padding the last one), pads the
/// chunk list with zero chunks up to @a _limit leaves and merkleizes it.
bytes sszMerkleize(bytes const& _data, size_t _limit)
{
	std::vector<bytes> nodes;
	for (size_t i = 0; i < _data.size(); i += 32)
	{
		bytes chunk(_data.begin() + static_cast<ptrdiff_t>(i), _data.begin() + static_cast<ptrdiff_t>(std::min(i + 32, _data.size())));
		chunk.resize(32, 0);
		nodes.emplace_back(std::move(chunk));
	}
	size_t leaves = 1;
	while (leaves < _limit)
		leaves *= 2;
	while (nodes.size() < leaves)
		nodes.emplace_back(bytes(32, 0));
	while (nodes.size() > 1)
	{
		std::vector<bytes> next;
		for (size_t i = 0; i < nodes.size(); i += 2)
			next.emplace_back(sszHashPair(nodes[i], nodes[i + 1]));
		nodes = std::move(next);
	}
	return nodes.front();
}

}

qrvmc::Result QRVMHost::precompileDepositRoot(qrvmc_message const& _message) noexcept
{
	// Input: DepositData fields concatenated without padding, in SSZ order:
	//   pubkey(2592) || withdrawal_recipient(64) || amount(8, little-endian) ||
	//   randao_commitment(32) || signature(4627)
	// Output: the 32-byte SSZ hash tree root of the DepositData container, as
	// returned by go-qrl (like sha256). Gas: flat 18000 (go-qrl DepositrootGas).
	size_t constexpr pubkeyLength = 2592;
	size_t constexpr withdrawalRecipientLength = 64;
	size_t constexpr amountLength = 8;
	size_t constexpr randaoCommitmentLength = 32;
	size_t constexpr signatureLength = 4627;
	size_t constexpr inputLength = pubkeyLength + withdrawalRecipientLength + amountLength + randaoCommitmentLength + signatureLength;

	if (_message.input_size != inputLength)
		return resultWithFailure();

	bytes const input(_message.input_data, _message.input_data + _message.input_size);
	auto field = [&](size_t _offset, size_t _length) {
		return bytes(input.begin() + static_cast<ptrdiff_t>(_offset), input.begin() + static_cast<ptrdiff_t>(_offset + _length));
	};
	size_t offset = 0;
	bytes const pubkey = field(offset, pubkeyLength);
	offset += pubkeyLength;
	bytes const withdrawalRecipient = field(offset, withdrawalRecipientLength);
	offset += withdrawalRecipientLength;
	bytes const amount = field(offset, amountLength);
	offset += amountLength;
	bytes const randaoCommitment = field(offset, randaoCommitmentLength);
	offset += randaoCommitmentLength;
	bytes const signature = field(offset, signatureLength);

	// Field roots: fixed-size byte vectors are merkleized over ceil(len / 32)
	// chunks; uint64 and bytes32 are single chunks.
	bytes fieldRoots;
	fieldRoots += sszMerkleize(pubkey, (pubkeyLength + 31) / 32);
	fieldRoots += sszMerkleize(withdrawalRecipient, (withdrawalRecipientLength + 31) / 32);
	fieldRoots += sszMerkleize(amount, 1);
	fieldRoots += sszMerkleize(randaoCommitment, 1);
	fieldRoots += sszMerkleize(signature, (signatureLength + 31) / 32);

	// static data so that we do not need a release routine...
	bytes static root;
	root = sszMerkleize(fieldRoots, 5);

	int64_t constexpr gas_cost = 18000;
	return resultWithGas(_message.gas, gas_cost, root);
}

qrvmc::Result QRVMHost::precompileXmssVerify(qrvmc_message const& _message) noexcept
{
	// Input: uint32 message_length (big-endian) || message || signature || extended_pk (67 bytes)
	// Output: like the other go-qrl verification precompiles, one 64-byte word (VMWordBytes)
	// holding 1 if the XMSS signature verifies with the semantics of qrllib's
	// XmssBase::verify (WOTS+ w = 16), and empty output otherwise. Malformed input yields
	// the empty (false) output instead of a failure, just like qrllib returns false.
	bytesConstRef const input(_message.input_data, _message.input_size);

	// Gas: the go-qrl xmssVerify precompile prices every internal hash invocation like a
	// sha256 precompile call over its at most 160-byte (three 64-byte words) input, i.e.
	// 60 + 3 * 12 = 96 gas, for the worst-case number of invocations: WOTS+ 67 chains *
	// 15 steps * 3 hashes, L-tree 66 nodes * 4 hashes, authentication path height * 4
	// hashes, plus one H_msg whose input is 128 bytes of key material followed by the
	// message. The height is taken from the key descriptor (maximum height if it is
	// malformed) and the message length from the prefix, capped by the input supplied.
	// Must match go-qrl core/vm xmssVerify.RequiredGas.
	unsigned height = xmssMaxHeight;
	if (input.size() >= xmssExtendedPublicKeySize)
	{
		unsigned const descriptorHeight = static_cast<unsigned>(input[input.size() - xmssExtendedPublicKeySize + 1] & 0x0f) << 1;
		if (descriptorHeight >= xmssMinHeight && descriptorHeight <= xmssMaxHeight)
			height = descriptorHeight;
	}
	int64_t messageLength = 0;
	if (input.size() >= xmssVerifyMessageLengthPrefixSize)
		messageLength = std::min<int64_t>(
			(static_cast<int64_t>(input[0]) << 24) |
			(static_cast<int64_t>(input[1]) << 16) |
			(static_cast<int64_t>(input[2]) << 8) |
			static_cast<int64_t>(input[3]),
			static_cast<int64_t>(input.size() - xmssVerifyMessageLengthPrefixSize)
		);
	int64_t constexpr gasPerHash = 60 + 12 * 3;
	int64_t const hashInvocations = 67 * 15 * 3 + 66 * 4 + 4 * static_cast<int64_t>(height);
	int64_t const gas_cost = gasPerHash * hashInvocations + 60 + 12 * ((128 + messageLength + 63) / 64);

	// static data so that we do not need a release routine...
	bytes static output;
	output.clear();
	if (xmssVerifyPrecompileInput(input))
	{
		output = bytes(64, 0);
		output[63] = 1;
	}
	return resultWithGas(_message.gas, gas_cost, output);
}

qrvmc::Result QRVMHost::precompileSha256(qrvmc_message const& _message) noexcept
{
	// static data so that we do not need a release routine...
	bytes static hash;
	bytes const rawHash = picosha2::hash256(bytes(
		_message.input_data,
		_message.input_data + _message.input_size
	));
	hash = rawHash;

	// Base 60 gas + 12 gas / 64-byte VM word.
	int64_t gas_cost = 60 + 12 * ((static_cast<int64_t>(_message.input_size) + 63) / 64);

	return resultWithGas(_message.gas, gas_cost, hash);
}

qrvmc::Result QRVMHost::precompileShake256(qrvmc_message const& _message) noexcept
{
	// Base 240 gas + 48 gas / 64-byte VM word.
	int64_t const gas_cost = 240 + 48 * ((static_cast<int64_t>(_message.input_size) + 63) / 64);
	if (_message.gas < gas_cost)
		return resultWithGas(_message.gas, gas_cost, {});

	bytes static const shortOutputMarker = asBytes("QRL-SHAKE256-SHORT-OUTPUT");
	bytes static const shortOutput(63, 0);
	if (
		_message.input_size == shortOutputMarker.size() &&
		std::equal(shortOutputMarker.begin(), shortOutputMarker.end(), _message.input_data)
	)
		return resultWithGas(_message.gas, gas_cost, shortOutput);

	bytes static hash;
	hash = shake256(bytesConstRef(_message.input_data, _message.input_size), 64);

	return resultWithGas(_message.gas, gas_cost, hash);
}

qrvmc::Result QRVMHost::precompileIdentity(qrvmc_message const& _message) noexcept
{
	// static data so that we do not need a release routine...
	bytes static data;
	data = bytes(_message.input_data, _message.input_data + _message.input_size);

	// Base 15 gas + 3 gas / 64-byte VM word.
	int64_t gas_cost = 15 + 3 * ((static_cast<int64_t>(_message.input_size) + 63) / 64);

	return resultWithGas(_message.gas, gas_cost, data);
}

qrvmc::Result QRVMHost::precompileModExp(qrvmc_message const&) noexcept
{
	// TODO implement
	return resultWithFailure();
}

qrvmc::Result QRVMHost::precompileMLDSA87Verify(qrvmc_message const& _message) noexcept
{
	// Match go-qrl's slot 3 verifier (pure ML-DSA-87 with the digest as the message):
	//   digest(64) || publicKey(2592) || signature(4627) || uint8(contextLength) || context
	size_t constexpr publicKeySize = MLDSA_PUBLICKEYBYTES(87);
	size_t constexpr signatureSize = MLDSA_BYTES(87);
	size_t constexpr publicKeyOffset = 64;
	size_t constexpr signatureOffset = publicKeyOffset + publicKeySize;
	size_t constexpr contextLengthOffset = signatureOffset + signatureSize;
	size_t constexpr contextOffset = contextLengthOffset + 1;
	int64_t constexpr gas_cost = 125000;
	// static data so that we do not need a release routine...
	bytes static const noData;
	bytes static const successWord = [] { bytes word(64, 0); word.back() = 1; return word; }();
	if (_message.gas < gas_cost)
		return resultWithGas(_message.gas, gas_cost, noData);

	uint8_t const* input = _message.input_data;
	size_t const inputSize = _message.input_size;
	if (inputSize < contextOffset || inputSize - contextOffset != input[contextLengthOffset])
		return resultWithGas(_message.gas, gas_cost, noData);

	// go-qrllib v0.10.0 ParsePublicKey rejects weak keys before FIPS 204 verification.
	// See https://github.com/theQRL/go-qrllib/blob/v0.10.0/crypto/ml_dsa_87/validate.go.
	// After rho (32 bytes), t1 packs four little-endian 10-bit coefficients per five
	// bytes. At least 76 must be in [96, 415] or [608, 927], matching ValidatePublicKey.
	unsigned largeCoefficients = 0;
	for (size_t i = 32; i < publicKeySize; i += 5)
	{
		uint64_t packed = 0;
		for (size_t j = 0; j < 5; ++j)
			packed |= uint64_t(input[publicKeyOffset + i + j]) << (8 * j);
		for (unsigned j = 0; j < 4; ++j)
		{
			unsigned const coefficient = static_cast<unsigned>((packed >> (10 * j)) & 1023);
			if ((coefficient >= 96 && coefficient <= 415) || (coefficient >= 608 && coefficient <= 927))
				++largeCoefficients;
		}
	}
	if (largeCoefficients < 76)
		return resultWithGas(_message.gas, gas_cost, noData);

	bool const valid = hyperion_mldsa87_verify(
		input + signatureOffset, input, publicKeyOffset,
		input + contextOffset, inputSize - contextOffset, input + publicKeyOffset
	) == 0;
	return resultWithGas(_message.gas, gas_cost, valid ? successWord : noData);
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
