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
 * @date 2016
 * Framework for executing contracts and testing them using RPC.
 */

#include <test/ExecutionFramework.h>

#include <test/QRVMHost.h>

#include <test/qrvmc/qrvmc.hpp>

#include <test/libhyperion/util/HyptestTypes.h>

#include <libhyputil/CommonIO.h>

#include <liblangutil/Exceptions.h>

#include <boost/test/framework.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <range/v3/range.hpp>
#include <range/v3/view/transform.hpp>

#include <cstdlib>
#include <limits>

using namespace std;
using namespace hyperion;
using namespace hyperion::util;
using namespace hyperion::test;
using namespace hyperion::frontend::test;

ExecutionFramework::ExecutionFramework():
	ExecutionFramework(hyperion::test::CommonOptions::get().qrvmVersion(), hyperion::test::CommonOptions::get().vmPaths)
{
}

ExecutionFramework::ExecutionFramework(langutil::QRVMVersion _qrvmVersion, vector<boost::filesystem::path> const& _vmPaths):
	m_qrvmVersion(_qrvmVersion),
	m_optimiserSettings(hyperion::frontend::OptimiserSettings::minimal()),
	m_showMessages(hyperion::test::CommonOptions::get().showMessages),
	m_vmPaths(_vmPaths)
{
	if (hyperion::test::CommonOptions::get().optimize)
		m_optimiserSettings = hyperion::frontend::OptimiserSettings::standard();
	selectVM(qrvmc_capabilities::QRVMC_CAPABILITY_QRVM1);
}

void ExecutionFramework::selectVM(qrvmc_capabilities _cap)
{
	m_qrvmcHost.reset();
	for (auto const& path: m_vmPaths)
	{
		qrvmc::VM& vm = QRVMHost::getVM(path.string());
		if (vm.has_capability(_cap))
		{
			m_qrvmcHost = make_unique<QRVMHost>(m_qrvmVersion, vm);
			break;
		}
	}
	hypAssert(m_qrvmcHost != nullptr, "");
	reset();
}

void ExecutionFramework::reset()
{
	m_qrvmcHost->reset();
	for (size_t i = 0; i < 10; i++)
		m_qrvmcHost->accounts[QRVMHost::convertToQRVMC(account(i))].balance =
			QRVMHost::convertToQRVMC(u256(1) << 100);
}

std::pair<bool, string> ExecutionFramework::compareAndCreateMessage(
	bytes const& _result,
	bytes const& _expectation
)
{
	if (_result == _expectation)
		return std::make_pair(true, std::string{});
	std::string message =
			"Invalid encoded data\n"
			"   Result                                                           Expectation\n";
	auto resultHex = boost::replace_all_copy(util::toHex(_result), "0", ".");
	auto expectedHex = boost::replace_all_copy(util::toHex(_expectation), "0", ".");
	for (size_t i = 0; i < std::max(resultHex.size(), expectedHex.size()); i += 0x40)
	{
		std::string result{i >= resultHex.size() ? string{} : resultHex.substr(i, 0x40)};
		std::string expected{i > expectedHex.size() ? string{} : expectedHex.substr(i, 0x40)};
		message +=
			(result == expected ? "   " : " X ") +
			result +
			std::string(0x41 - result.size(), ' ') +
			expected +
			"\n";
	}
	return make_pair(false, message);
}

bytes ExecutionFramework::panicData(util::PanicCode _code)
{
	return toCompactBigEndian(selectorFromSignatureU32("Panic(uint256)"), 4) + encode(u256(static_cast<unsigned>(_code)));
}

u256 ExecutionFramework::gasLimit() const
{
	return {m_qrvmcHost->tx_context.block_gas_limit};
}

u256 ExecutionFramework::gasPrice() const
{
	// here and below we use "return u256{....}" instead of just "return {....}"
	// to please MSVC and avoid unexpected
	// warning C4927 : illegal conversion; more than one user - defined conversion has been implicitly applied
	return u256{QRVMHost::convertFromQRVMC(m_qrvmcHost->tx_context.tx_gas_price)};
}

u256 ExecutionFramework::blockHash(u256 const& _number) const
{
	return u256{QRVMHost::convertFromQRVMC(
		m_qrvmcHost->get_block_hash(static_cast<int64_t>(_number & numeric_limits<uint64_t>::max()))
	)};
}

u256 ExecutionFramework::blockNumber() const
{
	return m_qrvmcHost->tx_context.block_number;
}

void ExecutionFramework::sendMessage(bytes const& _data, bool _isCreation, u256 const& _value)
{
	m_qrvmcHost->newBlock();

	if (m_showMessages)
	{
		if (_isCreation)
			cout << "CREATE " << m_sender.hex() << ":" << endl;
		else
			cout << "CALL   " << m_sender.hex() << " -> " << m_contractAddress.hex() << ":" << endl;
		if (_value > 0)
			cout << " value: " << _value << endl;
		cout << " in:      " << util::toHex(_data) << endl;
	}
	qrvmc_message message{};
	message.input_data = _data.data();
	message.input_size = _data.size();
	message.sender = QRVMHost::convertToQRVMC(m_sender);
	message.value = QRVMHost::convertToQRVMC(_value);

	if (_isCreation)
	{
		message.kind = QRVMC_CREATE;
		message.recipient = {};
		message.code_address = {};
	}
	else
	{
		message.kind = QRVMC_CALL;
		message.recipient = QRVMHost::convertToQRVMC(m_contractAddress);
		message.code_address = message.recipient;
	}

	message.gas = InitialGas.convert_to<int64_t>();

	qrvmc::Result result = m_qrvmcHost->call(message);

	m_output = bytes(result.output_data, result.output_data + result.output_size);
	if (_isCreation)
		m_contractAddress = QRVMHost::convertFromQRVMC(result.create_address);

	unsigned const refundRatio = 5;
	auto const totalGasUsed = InitialGas - result.gas_left;
	auto const gasRefund = min(u256(result.gas_refund), totalGasUsed / refundRatio);

	m_gasUsed = totalGasUsed - gasRefund;
	m_transactionSuccessful = (result.status_code == QRVMC_SUCCESS);

	if (m_showMessages)
	{
		cout << " out:                       " << util::toHex(m_output) << endl;
		cout << " result:                    " << static_cast<size_t>(result.status_code) << endl;
		cout << " gas used:                  " << m_gasUsed.str() << endl;
		cout << " gas used (without refund): " << totalGasUsed.str() << endl;
		cout << " gas refund (total):        " << result.gas_refund << endl;
		cout << " gas refund (bound):        " << gasRefund.str() << endl;
	}
}

void ExecutionFramework::sendQuanta(h160 const& _addr, u256 const& _amount)
{
	m_qrvmcHost->newBlock();

	if (m_showMessages)
	{
		cout << "SEND_QUANTA   " << m_sender.hex() << " -> " << _addr.hex() << ":" << endl;
		if (_amount > 0)
			cout << " value: " << _amount << endl;
	}
	qrvmc_message message{};
	message.sender = QRVMHost::convertToQRVMC(m_sender);
	message.value = QRVMHost::convertToQRVMC(_amount);
	message.kind = QRVMC_CALL;
	message.recipient = QRVMHost::convertToQRVMC(_addr);
	message.code_address = message.recipient;
	message.gas = InitialGas.convert_to<int64_t>();

	m_qrvmcHost->call(message);
}

size_t ExecutionFramework::currentTimestamp()
{
	return static_cast<size_t>(m_qrvmcHost->tx_context.block_timestamp);
}

size_t ExecutionFramework::blockTimestamp(u256 _block)
{
	if (_block > blockNumber())
		return 0;
	else
		return static_cast<size_t>((currentTimestamp() / blockNumber()) * _block);
}

h160 ExecutionFramework::account(size_t _idx)
{
	return h160(h256(u256{"0x1212121212121212121212121212120000000012"} + _idx * 0x1000), h160::AlignRight);
}

bool ExecutionFramework::addressHasCode(h160 const& _addr) const
{
	return m_qrvmcHost->get_code_size(QRVMHost::convertToQRVMC(_addr)) != 0;
}

size_t ExecutionFramework::numLogs() const
{
	return m_qrvmcHost->recorded_logs.size();
}

size_t ExecutionFramework::numLogTopics(size_t _logIdx) const
{
	return m_qrvmcHost->recorded_logs.at(_logIdx).topics.size();
}

h256 ExecutionFramework::logTopic(size_t _logIdx, size_t _topicIdx) const
{
	return QRVMHost::convertFromQRVMC(m_qrvmcHost->recorded_logs.at(_logIdx).topics.at(_topicIdx));
}

h160 ExecutionFramework::logAddress(size_t _logIdx) const
{
	return QRVMHost::convertFromQRVMC(m_qrvmcHost->recorded_logs.at(_logIdx).creator);
}

bytes ExecutionFramework::logData(size_t _logIdx) const
{
	auto const& data = m_qrvmcHost->recorded_logs.at(_logIdx).data;
	// TODO: Return a copy of log data, because this is expected from REQUIRE_LOG_DATA(),
	//       but reference type like string_view would be preferable.
	return {data.begin(), data.end()};
}

u256 ExecutionFramework::balanceAt(h160 const& _addr) const
{
	return u256(QRVMHost::convertFromQRVMC(m_qrvmcHost->get_balance(QRVMHost::convertToQRVMC(_addr))));
}

bool ExecutionFramework::storageEmpty(h160 const& _addr) const
{
	const auto it = m_qrvmcHost->accounts.find(QRVMHost::convertToQRVMC(_addr));
	if (it != m_qrvmcHost->accounts.end())
	{
		for (auto const& entry: it->second.storage)
			if (entry.second.current != qrvmc::bytes32{})
				return false;
	}
	return true;
}

vector<hyperion::frontend::test::LogRecord> ExecutionFramework::recordedLogs() const
{
	vector<LogRecord> logs;
	for (qrvmc::MockedHost::log_record const& logRecord: m_qrvmcHost->recorded_logs)
		logs.emplace_back(
			QRVMHost::convertFromQRVMC(logRecord.creator),
			bytes{logRecord.data.begin(), logRecord.data.end()},
			logRecord.topics | ranges::views::transform([](qrvmc::bytes32 _bytes) { return QRVMHost::convertFromQRVMC(_bytes); }) | ranges::to<vector>
		);
	return logs;
}
