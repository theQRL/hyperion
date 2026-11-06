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
#include <algorithm>
#include <cstddef>
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

	/// Accounts for a chain of nodes that a loop builds inside out, such as the array suffixes of
	/// a type or the member accesses, index accesses and calls after a primary expression, so that
	/// the depth of the resulting tree stays within the recursion limit. The passes that follow the
	/// parser visit the tree recursively, so such a chain has to obey the same budget as nested
	/// recursion. Each node of the chain becomes the root of the chain built so far, with
	/// everything parsed before it one level deeper, and a branch hanging off a node (an array
	/// length, an index, call arguments or options) ends up below every node added after it.
	class NestingGuard
	{
	public:
		explicit NestingGuard(ParserBase& _parser):
			m_parser(_parser),
			m_entryDepth(_parser.m_recursionDepth)
		{}
		/// Accounts for one more node of the chain.
		void addLevel()
		{
			++m_links;
			check();
		}
		/// Parses a branch of the chain's newest node with @a _parse and accounts for its depth.
		template<typename Parse>
		auto parseBranch(Parse&& _parse)
		{
			size_t const outerWatermark = m_parser.m_recursionDepthWatermark;
			m_parser.m_recursionDepthWatermark = m_parser.m_recursionDepth;
			auto result = _parse();
			size_t const branchDepth = m_parser.m_recursionDepthWatermark - m_entryDepth;
			m_parser.m_recursionDepthWatermark = std::max(outerWatermark, m_parser.m_recursionDepthWatermark);
			// Once the chain has n nodes, the branch of node j sits (n - j + 1) levels below the
			// root plus its own depth. The part that does not depend on n is kept here, and check()
			// adds the number of nodes so far.
			m_deepestBranch = std::max(
				m_deepestBranch,
				static_cast<std::ptrdiff_t>(branchDepth) + 1 - static_cast<std::ptrdiff_t>(m_links)
			);
			check();
			return result;
		}
		/// Continues a chain whose first @a _links nodes were parsed ahead, with the branch
		/// accounting @a _deepestBranch of the guard that parsed them.
		void adopt(size_t _links, std::ptrdiff_t _deepestBranch)
		{
			m_links += _links;
			m_deepestBranch = std::max(m_deepestBranch, _deepestBranch);
			check();
		}
		size_t links() const { return m_links; }
		std::ptrdiff_t deepestBranch() const { return m_deepestBranch; }
	private:
		void check() const
		{
			m_parser.checkRecursionDepth(
				m_entryDepth + m_links + static_cast<size_t>(std::max<std::ptrdiff_t>(m_deepestBranch, 0))
			);
		}
		ParserBase& m_parser;
		size_t m_entryDepth;
		size_t m_links = 0;
		std::ptrdiff_t m_deepestBranch = 0;
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
	/// Throws the same exception as increaseRecursionDepth() if @a _depth is too deep, for depth
	/// that a NestingGuard accounts for after the fact.
	void checkRecursionDepth(size_t _depth);

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
	/// Deepest recursion depth reached since a NestingGuard last reset it, to measure a branch.
	size_t m_recursionDepthWatermark = 0;
};

}
