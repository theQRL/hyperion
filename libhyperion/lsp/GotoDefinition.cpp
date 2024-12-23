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
#include <libhyperion/lsp/GotoDefinition.h>
#include <libhyperion/lsp/Transport.h> // for RequestError
#include <libhyperion/lsp/Utils.h>
#include <libhyperion/ast/AST.h>
#include <libhyperion/ast/ASTUtils.h>

#include <fmt/format.h>

#include <memory>
#include <string>
#include <vector>

using namespace hyperion::frontend;
using namespace hyperion::langutil;
using namespace hyperion::lsp;

void GotoDefinition::operator()(MessageID _id, Json::Value const& _args)
{
	auto const [sourceUnitName, lineColumn] = extractSourceUnitNameAndLineColumn(_args);

	ASTNode const* sourceNode = m_server.astNodeAtSourceLocation(sourceUnitName, lineColumn);

	std::vector<SourceLocation> locations;
	if (auto const* expression = dynamic_cast<Expression const*>(sourceNode))
	{
		// Handles all expressions that can have one or more declaration annotation.
		if (auto const* declaration = referencedDeclaration(expression))
			if (auto location = declarationLocation(declaration))
				locations.emplace_back(std::move(location.value()));
	}
	else if (auto const* identifierPath = dynamic_cast<IdentifierPath const*>(sourceNode))
	{
		if (auto const* declaration = identifierPath->annotation().referencedDeclaration)
			if (auto location = declarationLocation(declaration))
				locations.emplace_back(std::move(location.value()));
	}
	else if (auto const* importDirective = dynamic_cast<ImportDirective const*>(sourceNode))
	{
		auto const& path = *importDirective->annotation().absolutePath;
		if (fileRepository().sourceUnits().count(path))
			locations.emplace_back(SourceLocation{0, 0, std::make_shared<std::string const>(path)});
	}

	Json::Value reply = Json::arrayValue;
	for (SourceLocation const& location: locations)
		reply.append(toJson(location));
	client().reply(_id, reply);
}
