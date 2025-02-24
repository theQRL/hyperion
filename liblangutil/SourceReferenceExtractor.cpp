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
#include <liblangutil/SourceReferenceExtractor.h>
#include <liblangutil/Exceptions.h>
#include <liblangutil/CharStreamProvider.h>
#include <liblangutil/CharStream.h>

#include <algorithm>
#include <cmath>
#include <variant>

using namespace hyperion;
using namespace hyperion::langutil;

SourceReferenceExtractor::Message SourceReferenceExtractor::extract(
	CharStreamProvider const& _charStreamProvider,
	util::Exception const& _exception,
	std::variant<Error::Type, Error::Severity> _typeOrSeverity
)
{
	SourceLocation const* location = boost::get_error_info<errinfo_sourceLocation>(_exception);

	std::string const* message = boost::get_error_info<util::errinfo_comment>(_exception);
	SourceReference primary = extract(_charStreamProvider, location, message ? *message : "");

	std::vector<SourceReference> secondary;
	auto secondaryLocation = boost::get_error_info<errinfo_secondarySourceLocation>(_exception);
	if (secondaryLocation && !secondaryLocation->infos.empty())
		for (auto const& info: secondaryLocation->infos)
			secondary.emplace_back(extract(_charStreamProvider, &info.second, info.first));

	return Message{std::move(primary), _typeOrSeverity, std::move(secondary), std::nullopt};
}

SourceReferenceExtractor::Message SourceReferenceExtractor::extract(
	CharStreamProvider const& _charStreamProvider,
	Error const& _error,
	std::variant<Error::Type, Error::Severity> _typeOrSeverity
)
{
	Message message = extract(_charStreamProvider, static_cast<util::Exception>(_error), _typeOrSeverity);
	message.errorId = _error.errorId();
	return message;
}

SourceReference SourceReferenceExtractor::extract(
	CharStreamProvider const& _charStreamProvider,
	SourceLocation const* _location,
	std::string message
)
{
	if (!_location || !_location->sourceName) // Nothing we can extract here
		return SourceReference::MessageOnly(std::move(message));

	if (!_location->hasText()) // No source text, so we can only extract the source name
		return SourceReference::MessageOnly(std::move(message), *_location->sourceName);

	CharStream const& charStream = _charStreamProvider.charStream(*_location->sourceName);

	LineColumn const interest = charStream.translatePositionToLineColumn(_location->start);
	LineColumn start = interest;
	LineColumn end = charStream.translatePositionToLineColumn(_location->end);
	bool const isMultiline = start.line != end.line;

	std::string line = charStream.lineAtPosition(_location->start);

	int locationLength =
		isMultiline ?
			int(line.length()) - start.column :
			end.column - start.column;

	if (locationLength > 150)
	{
		auto const lhs = static_cast<size_t>(start.column) + 35;
		std::string::size_type const rhs = (isMultiline ? line.length() : static_cast<size_t>(end.column)) - 35;
		line = line.substr(0, lhs) + " ... " + line.substr(rhs);
		end.column = start.column + 75;
		locationLength = 75;
	}

	if (line.length() > 150)
	{
		int const len = static_cast<int>(line.length());
		line = line.substr(
			static_cast<size_t>(std::max(0, start.column - 35)),
			static_cast<size_t>(std::min(start.column, 35)) + static_cast<size_t>(
				std::min(locationLength + 35, len - start.column)
			)
		);
		if (start.column + locationLength + 35 < len)
			line += " ...";
		if (start.column > 35)
		{
			line = " ... " + line;
			start.column = 40;
		}
		end.column = start.column + static_cast<int>(locationLength);
	}

	return SourceReference{
		std::move(message),
		*_location->sourceName,
		interest,
		isMultiline,
		line,
		std::min(start.column, static_cast<int>(line.length())),
		std::min(end.column, static_cast<int>(line.length()))
	};
}
