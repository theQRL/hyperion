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

#pragma once

#include <test/libhyperion/util/HyptestTypes.h>

#include <test/libhyperion/util/HyptestErrors.h>

#include <libhyputil/CommonData.h>

#include <json/json.h>

namespace hyperion::frontend::test
{

using ABITypes = std::vector<ABIType>;

/**
 * Utility class that aids conversions from contract ABI types stored in a
 * Json value to the internal ABIType representation of ihyptest.
 */
class ContractABIUtils
{
public:
	/// Parses and translates Hyperion's ABI types as Json string into
	/// a list of internal type representations of ihyptest.
	/// Creates parameters from Contract ABI and is used to generate values for
	/// auto-correction during interactive update routine.
	static std::optional<ParameterList> parametersFromJsonOutputs(
		ErrorReporter& _errorReporter,
		Json::Value const& _contractABI,
		std::string const& _functionSignature
	);

	/// Overwrites _targetParameters if ABI types or sizes given
	/// by _sourceParameters do not match.
	static void overwriteParameters(
		ErrorReporter& _errorReporter,
		ParameterList& _targetParameters,
		ParameterList const& _sourceParameters
	);

	/// If parameter count does not match, take types defined _sourceParameters
	/// and create a warning if so.
	static ParameterList preferredParameters(
		ErrorReporter& _errorReporter,
		ParameterList const& _targetParameters,
		ParameterList const& _sourceParameters,
		bytes const& _bytes
	);

	/// Returns a list of parameters corresponding to the encoding of
	/// returned values in case of a failure. Creates an additional parameter
	/// for the error message if _bytes is larger than 68 bytes
	/// (function_selector + tail_ptr + message_length).
	static ParameterList failureParameters(bytes const& _bytes);

	/// Returns _count parameters with their type set to ABIType::UnsignedDec
	/// and their size set to 32 bytes.
	static ParameterList defaultParameters(size_t count = 0);

	/// Calculates the encoding size of given _parameters based
	/// on the size of their types.
	static size_t encodingSize(ParameterList const& _paremeters);

private:
	/// Parses and translates a single type and returns a list of
	/// internal type representations of ihyptest.
	/// Types defined by the ABI will translate to ABITypes
	/// as follows:
	/// `bool` -> [`Boolean`]
	/// `uint` -> [`Unsigned`]
	/// `string` -> [`Unsigned`, `Unsigned`, `String`]
	/// `bytes` -> [`Unsigned`, `Unsigned`, `HexString`]
	/// ...
	static bool appendTypesFromName(
		Json::Value const& _functionOutput,
		ABITypes& _inplaceTypes,
		ABITypes& _dynamicTypes,
		bool _isCompoundType = false
	);
};

}
