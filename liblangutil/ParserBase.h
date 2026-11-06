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
 * Hyperion parser shared functionality.
 */

#pragma once

#include <liblangutil/Token.h>
#include <memory>
#include <string>

namespace hyperion::langutil
{

class ErrorReporter;
class Scanner;
struct SourceLocation;
struct ErrorId;

class ParserBase
{
public:
	explicit ParserBase(ErrorReporter& errorReporter):
		m_errorReporter(errorReporter)
	{}

	virtual ~ParserBase() = default;

protected:
	/// Utility class that creates an error and throws an exception if the
	/// recursion depth is too deep.
	class RecursionGuard
	{
	public:
		explicit RecursionGuard(ParserBase& _parser): m_parser(_parser)
		{
			m_parser.increaseRecursionDepth();
		}
		~RecursionGuard() { m_parser.decreaseRecursionDepth(); }
	private:
		ParserBase& m_parser;
	};

	/// Counts the nesting levels that a loop adds to the syntax tree against the recursion
	/// depth, as if the loop had recursed, and releases them all when it goes out of scope.
	/// The passes that follow the parser visit the tree recursively, so a chain that the parser
	/// builds iteratively has to stay within the same budget as nested recursion.
	class NestingGuard
	{
	public:
		explicit NestingGuard(ParserBase& _parser): m_parser(_parser) {}
		~NestingGuard()
		{
			for (; m_levels > 0; --m_levels)
				m_parser.decreaseRecursionDepth();
		}
		void addLevel()
		{
			m_parser.increaseRecursionDepth();
			++m_levels;
		}
	private:
		ParserBase& m_parser;
		size_t m_levels = 0;
	};

	/// Location of the current token
	virtual SourceLocation currentLocation() const;

	///@{
	///@name Helper functions
	/// If current token value is not @a _value, throw exception otherwise advance token
	//  if @a _advance is true
	void expectToken(Token _value, bool _advance = true);

	Token currentToken() const;
	Token peekNextToken() const;
	std::string tokenName(Token _token);
	std::string currentLiteral() const;
	virtual Token advance();
	///@}

	/// Increases the recursion depth and throws an exception if it is too deep.
	void increaseRecursionDepth();
	void decreaseRecursionDepth();

	/// Creates a @ref ParserError and annotates it with the current position and the
	/// given @a _description.
	void parserError(ErrorId _error, std::string const& _description);
	void parserError(ErrorId _error, SourceLocation const& _location, std::string const& _description);

	/// Creates a @ref ParserWarning and annotates it with the current position and the
	/// given @a _description.
	void parserWarning(ErrorId _error, std::string const& _description);
	void parserWarning(ErrorId _error, SourceLocation const& _location, std::string const& _description);

	/// Creates a @ref ParserError and annotates it with the current position and the
	/// given @a _description. Throws the FatalError.
	void fatalParserError(ErrorId _error, std::string const& _description);
	void fatalParserError(ErrorId _error, SourceLocation const& _location, std::string const& _description);

	std::shared_ptr<Scanner> m_scanner;
	/// The reference to the list of errors, warnings and infos to add errors/warnings/infos during parsing
	ErrorReporter& m_errorReporter;
	/// Current recursion depth during parsing.
	size_t m_recursionDepth = 0;
};

}
