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

#include <libhyperion/lsp/DocumentHoverHandler.h>
#include <libhyperion/lsp/Utils.h>

#include <fmt/format.h>

namespace hyperion::lsp
{
using namespace hyperion::lsp;
using namespace hyperion::langutil;
using namespace hyperion::frontend;

namespace
{

struct MarkdownBuilder
{
	std::stringstream result;

	MarkdownBuilder& hyperionCode(std::string const& _code)
	{
		auto constexpr HyperionLanguageId = "hyperion";
		result << "```" << HyperionLanguageId << '\n' << _code << "\n```\n\n";
		return *this;
	}

	MarkdownBuilder& paragraph(std::string const& _text)
	{
		if (!_text.empty())
		{
			result << _text << '\n';
			if (_text.back() != '\n') // We want double-LF to ensure constructing a paragraph.
				result << '\n';
		}
		return *this;
	}
};

}

void DocumentHoverHandler::operator()(MessageID _id, Json::Value const& _args)
{
	auto const [sourceUnitName, lineColumn] = HandlerBase(*this).extractSourceUnitNameAndLineColumn(_args);
	auto const [sourceNode, sourceOffset] = m_server.astNodeAndOffsetAtSourceLocation(sourceUnitName, lineColumn);

	MarkdownBuilder markdown;
	auto rangeToHighlight = toRange(sourceNode->location());

	// Try getting the type definition of the underlying AST node, if available.
	if (auto const* expression = dynamic_cast<Expression const*>(sourceNode))
	{
		if (auto const* declaration = ASTNode::referencedDeclaration(*expression))
			if (declaration->type())
				markdown.hyperionCode(declaration->type()->toString(false));
	}
	else if (auto const* declaration = dynamic_cast<Declaration const*>(sourceNode))
	{
		if (declaration->type())
			markdown.hyperionCode(declaration->type()->toString(false));
	}
	else if (auto const* identifierPath = dynamic_cast<IdentifierPath const*>(sourceNode))
	{
		for (size_t i = 0; i < identifierPath->path().size(); ++i)
		{
			if (identifierPath->pathLocations()[i].containsOffset(sourceOffset))
			{
				rangeToHighlight = toRange(identifierPath->pathLocations()[i]);

				if (i < identifierPath->annotation().pathDeclarations.size())
				{
					Declaration const* declaration = identifierPath->annotation().pathDeclarations[i];
					if (declaration && declaration->type())
						markdown.hyperionCode(declaration->type()->toString(false));
					if (auto const* structurallyDocumented = dynamic_cast<StructurallyDocumented const*>(declaration))
						if (structurallyDocumented->documentation()->text())
							markdown.paragraph(*structurallyDocumented->documentation()->text());
				}
				break;
			}
		}
	}

	// If this AST node contains documentation itself, append it.
	if (auto const* documented = dynamic_cast<StructurallyDocumented const*>(sourceNode))
	{
		if (documented->documentation())
			markdown.paragraph(*documented->documentation()->text());
	}

	auto tooltipText = markdown.result.str();

	if (tooltipText.empty())
	{
		client().reply(_id, Json::nullValue);
		return;
	}

	Json::Value reply = Json::objectValue;
	reply["range"] = rangeToHighlight;
	reply["contents"]["kind"] = "markdown";
	reply["contents"]["value"] = std::move(tooltipText);

	client().reply(_id, reply);
}

}
