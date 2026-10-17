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

#include <libhyperion/lsp/LanguageServer.h>
#include <libhyperion/lsp/Transport.h>
#include <libhyperion/lsp/Utils.h>

#include <test/FilesystemUtils.h>

#include <libhyputil/JSON.h>
#include <libhyputil/TemporaryDirectory.h>

#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace std::string_literals;

namespace hyperion::lsp::test
{

namespace
{

std::string frame(std::string const& _payload)
{
	return "Content-Length: " + std::to_string(_payload.size()) + "\r\n\r\n" + _payload;
}

bool containsParseError(std::string const& _output, std::string const& _message)
{
	return
		_output.find("\"code\":-32700") != std::string::npos &&
		_output.find(_message) != std::string::npos;
}

bool containsError(std::string const& _output, ErrorCode _code, std::string const& _message)
{
	return
		_output.find("\"code\":" + std::to_string(static_cast<int>(_code))) != std::string::npos &&
		_output.find(_message) != std::string::npos;
}

std::string payload(std::string const& _method, Json::Value _params = Json::objectValue, Json::Value _id = Json::nullValue)
{
	Json::Value message;
	message["jsonrpc"] = "2.0";
	message["method"] = _method;
	message["params"] = std::move(_params);
	if (_id != Json::nullValue)
		message["id"] = std::move(_id);
	return util::jsonCompactPrint(message);
}

std::string runLanguageServer(std::vector<std::string> const& _payloads)
{
	std::string inputString;
	for (std::string const& message: _payloads)
		inputString += frame(message);

	std::istringstream input{inputString};
	std::ostringstream output;
	IOStreamTransport transport{input, output};
	BOOST_CHECK(LanguageServer{transport}.run());
	return output.str();
}

std::string canonicalFileUri(boost::filesystem::path const& _path)
{
	std::string path = boost::filesystem::canonical(_path).generic_string();
	return (path.front() == '/' ? "file://" : "file:///") + path;
}

Json::Value initializeParams(boost::filesystem::path const& _rootPath)
{
	Json::Value params;
	params["rootPath"] = _rootPath.generic_string();
	params["initializationOptions"]["file-load-strategy"] = "directly-opened-and-on-import";
	return params;
}

Json::Value didOpenParams(boost::filesystem::path const& _path, std::string _text)
{
	Json::Value params;
	params["textDocument"]["uri"] = "file://" + _path.generic_string();
	params["textDocument"]["text"] = std::move(_text);
	return params;
}

Json::Value didOpenParamsWithUri(std::string _uri, std::string _text)
{
	Json::Value params;
	params["textDocument"]["uri"] = std::move(_uri);
	params["textDocument"]["text"] = std::move(_text);
	return params;
}

Json::Value textDocumentParamsWithUri(std::string _uri)
{
	Json::Value params;
	params["textDocument"]["uri"] = std::move(_uri);
	return params;
}

Json::Value textDocumentParams(boost::filesystem::path const& _path)
{
	Json::Value params;
	params["textDocument"]["uri"] = "file://" + _path.generic_string();
	return params;
}

Json::Value textDocumentPositionParams(boost::filesystem::path const& _path, int _line, int _character)
{
	Json::Value params = textDocumentParams(_path);
	params["position"]["line"] = _line;
	params["position"]["character"] = _character;
	return params;
}

Json::Value renameParams(boost::filesystem::path const& _path, int _line, int _character, std::string const& _newName)
{
	Json::Value params = textDocumentPositionParams(_path, _line, _character);
	params["newName"] = _newName;
	return params;
}

}

BOOST_AUTO_TEST_SUITE(LSPTransport)

BOOST_AUTO_TEST_CASE(receive_accepts_valid_message)
{
	std::string const payload = R"({"jsonrpc":"2.0","id":1,"method":"test"})";
	std::istringstream input{frame(payload)};
	std::ostringstream output;
	IOStreamTransport transport{input, output};

	auto message = transport.receive();
	BOOST_REQUIRE(message);
	BOOST_CHECK_EQUAL((*message)["method"].asString(), "test");
	BOOST_CHECK(output.str().empty());
}

BOOST_AUTO_TEST_CASE(receive_rejects_invalid_content_length)
{
	std::istringstream input{"Content-Length: nope\r\n\r\n"};
	std::ostringstream output;
	IOStreamTransport transport{input, output};

	BOOST_CHECK(!transport.receive());
	BOOST_CHECK(containsParseError(output.str(), "Invalid Content-Length header."));
}

BOOST_AUTO_TEST_CASE(receive_rejects_oversized_content_length_without_consuming_next_message)
{
	std::string const payload = R"({"jsonrpc":"2.0","id":1,"method":"test"})";
	std::istringstream input{
		"Content-Length: "s + std::to_string(std::numeric_limits<size_t>::max()) + "\r\n\r\n" +
		frame(payload)
	};
	std::ostringstream output;
	IOStreamTransport transport{input, output};

	BOOST_CHECK(!transport.receive());
	BOOST_CHECK(containsParseError(output.str(), "Content-Length exceeds maximum supported size"));

	auto message = transport.receive();
	BOOST_REQUIRE(message);
	BOOST_CHECK_EQUAL((*message)["method"].asString(), "test");
}

BOOST_AUTO_TEST_CASE(receive_rejects_short_body)
{
	std::istringstream input{"Content-Length: 10\r\n\r\n{}"};
	std::ostringstream output;
	IOStreamTransport transport{input, output};

	BOOST_CHECK(!transport.receive());
	BOOST_CHECK(containsParseError(output.str(), "Unexpected end of input while reading RPC payload."));
}

BOOST_AUTO_TEST_CASE(receive_rejects_oversized_header_line)
{
	std::istringstream input{"X-Test: " + std::string(9000, 'a') + "\r\n\r\n"};
	std::ostringstream output;
	IOStreamTransport transport{input, output};

	BOOST_CHECK(!transport.receive());
	BOOST_CHECK(transport.closed());
	BOOST_CHECK(containsParseError(output.str(), "Could not parse RPC headers."));
}

BOOST_AUTO_TEST_CASE(receive_rejects_too_many_headers)
{
	std::string inputString;
	for (size_t i = 0; i < 65; ++i)
		inputString += "X-Test-" + std::to_string(i) + ": value\r\n";
	inputString += "\r\n";
	std::istringstream input{inputString};
	std::ostringstream output;
	IOStreamTransport transport{input, output};

	BOOST_CHECK(!transport.receive());
	BOOST_CHECK(transport.closed());
	BOOST_CHECK(containsParseError(output.str(), "Could not parse RPC headers."));
}

BOOST_AUTO_TEST_CASE(receive_rejects_oversized_header_block)
{
	std::string inputString;
	for (size_t i = 0; i < 17; ++i)
		inputString += "X-Test-" + std::to_string(i) + ": " + std::string(4090, 'a') + "\r\n";
	inputString += "\r\n";
	std::istringstream input{inputString};
	std::ostringstream output;
	IOStreamTransport transport{input, output};

	BOOST_CHECK(!transport.receive());
	BOOST_CHECK(transport.closed());
	BOOST_CHECK(containsParseError(output.str(), "Could not parse RPC headers."));
}

BOOST_AUTO_TEST_CASE(language_server_uses_legacy_root_path_for_imports)
{
	util::TemporaryDirectory tempDir{"lsp-root-path-test"};
	boost::filesystem::path importedFile = tempDir.path() / "Imported.hyp";
	{
		std::ofstream outFile(importedFile.string());
		outFile << "contract Imported {}" << std::endl;
	}

	std::string output = runLanguageServer({
		payload("initialize", initializeParams(tempDir.path()), 1),
		payload(
			"textDocument/didOpen",
			didOpenParams(tempDir.path() / "main.hyp", "import \"Imported.hyp\";\ncontract Main {}")
		),
		payload("shutdown", Json::nullValue, 2),
		payload("exit", Json::nullValue)
	});

	BOOST_CHECK_EQUAL(output.find("File not found"), std::string::npos);
	BOOST_CHECK(output.find(canonicalFileUri(importedFile)) != std::string::npos);
}

BOOST_AUTO_TEST_CASE(language_server_without_root_uses_opened_files_only)
{
	std::string output = runLanguageServer({
		payload("initialize", Json::objectValue, 1),
		payload("initialized"),
		payload("shutdown", Json::nullValue, 2),
		payload("exit", Json::nullValue)
	});

	BOOST_CHECK(output.find("\"id\":1") != std::string::npos);
	BOOST_CHECK_EQUAL(output.find("textDocument/publishDiagnostics"), std::string::npos);
	BOOST_CHECK_EQUAL(output.find("Unhandled exception"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(language_server_malformed_did_open_does_not_poison_open_files)
{
	util::TemporaryDirectory tempDir{"lsp-malformed-did-open-test"};
	boost::filesystem::path sourceFile = tempDir.path() / "main.hyp";

	std::string output = runLanguageServer({
		payload("initialize", initializeParams(tempDir.path()), 1),
		payload(
			"textDocument/didOpen",
			didOpenParamsWithUri("https://example.invalid/main.hyp", "contract Bad {}")
		),
		payload(
			"textDocument/didOpen",
			didOpenParams(sourceFile, "contract C {}")
		),
		payload("textDocument/semanticTokens/full", textDocumentParams(sourceFile), 2),
		payload("shutdown", Json::nullValue, 3),
		payload("exit", Json::nullValue)
	});

	BOOST_CHECK(containsError(output, ErrorCode::InvalidParams, "URI must start with file://"));
	BOOST_CHECK(output.find("\"id\":2") != std::string::npos);
	BOOST_CHECK(output.find("\"data\":[") != std::string::npos);
	BOOST_CHECK_EQUAL(output.find("\"code\":-32603"), std::string::npos);
	BOOST_CHECK_EQUAL(output.find("Unhandled exception"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(language_server_rejects_project_directory_without_root)
{
	Json::Value params;
	params["initializationOptions"]["file-load-strategy"] = "project-directory";

	std::string output = runLanguageServer({
		payload("initialize", params, 1),
		payload("shutdown", Json::nullValue, 2),
		payload("exit", Json::nullValue)
	});

	BOOST_CHECK(containsError(output, ErrorCode::InvalidParams, "requires rootUri or rootPath"));
	BOOST_CHECK_EQUAL(output.find("Unhandled exception"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(language_server_rejects_absolute_imports_outside_workspace)
{
	util::TemporaryDirectory workspaceDir{"lsp-workspace-test"};
	util::TemporaryDirectory outsideDir{"lsp-outside-test"};
	boost::filesystem::path outsideFile = outsideDir.path() / "Outside.hyp";
	{
		std::ofstream outFile(outsideFile.string());
		outFile << "contract Outside {}" << std::endl;
	}

	std::string output = runLanguageServer({
		payload("initialize", initializeParams(workspaceDir.path()), 1),
		payload(
			"textDocument/didOpen",
			didOpenParams(
				workspaceDir.path() / "main.hyp",
				"import \"" + outsideFile.generic_string() + "\";\ncontract Main {}"
			)
		),
		payload("shutdown", Json::nullValue, 2),
		payload("exit", Json::nullValue)
	});

	BOOST_CHECK(output.find("File outside of allowed directories") != std::string::npos);
	BOOST_CHECK_EQUAL(output.find("\"uri\":\"" + canonicalFileUri(outsideFile) + "\""), std::string::npos);
}

BOOST_AUTO_TEST_CASE(language_server_project_directory_does_not_follow_directory_symlinks_outside_workspace)
{
	util::TemporaryDirectory workspaceDir{"lsp-project-symlink-workspace-test"};
	util::TemporaryDirectory outsideDir{"lsp-project-symlink-outside-test"};

	boost::filesystem::path insideFile = workspaceDir.path() / "Inside.hyp";
	{
		std::ofstream outFile(insideFile.string());
		outFile << "contract Inside {}" << std::endl;
	}

	boost::filesystem::path outsideFile = outsideDir.path() / "Outside.hyp";
	{
		std::ofstream outFile(outsideFile.string());
		outFile << "contract Outside {}" << std::endl;
	}

	if (!hyperion::test::createSymlinkIfSupportedByFilesystem(outsideDir.path(), workspaceDir.path() / "linked", true))
		return;

	Json::Value params;
	params["rootPath"] = workspaceDir.path().generic_string();
	params["initializationOptions"]["file-load-strategy"] = "project-directory";

	std::string output = runLanguageServer({
		payload("initialize", params, 1),
		payload("initialized"),
		payload("shutdown", Json::nullValue, 2),
		payload("exit", Json::nullValue)
	});

	// Project discovery resolves symlinks, including macOS temporary directory parents.
	BOOST_CHECK(output.find("\"uri\":\"" + canonicalFileUri(insideFile) + "\"") != std::string::npos);
	BOOST_CHECK_EQUAL(output.find("\"uri\":\"" + canonicalFileUri(outsideFile) + "\""), std::string::npos);
	BOOST_CHECK_EQUAL(output.find("Unhandled exception"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(language_server_semantic_tokens_requires_initialization)
{
	util::TemporaryDirectory tempDir{"lsp-semantic-tokens-pre-init-test"};
	boost::filesystem::path sourceFile = tempDir.path() / "main.hyp";

	std::string output = runLanguageServer({
		payload("textDocument/semanticTokens/full", textDocumentParams(sourceFile), 1),
		payload("initialize", initializeParams(tempDir.path()), 2),
		payload("shutdown", Json::nullValue, 3),
		payload("exit", Json::nullValue)
	});

	BOOST_CHECK(containsError(output, ErrorCode::ServerNotInitialized, "Server is not properly initialized."));
	BOOST_CHECK_EQUAL(output.find("\"code\":-32603"), std::string::npos);
	BOOST_CHECK_EQUAL(output.find("Unhandled exception"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(language_server_semantic_tokens_rejects_unknown_uri)
{
	util::TemporaryDirectory tempDir{"lsp-semantic-tokens-unknown-uri-test"};
	boost::filesystem::path sourceFile = tempDir.path() / "missing.hyp";

	std::string output = runLanguageServer({
		payload("initialize", initializeParams(tempDir.path()), 1),
		payload("textDocument/semanticTokens/full", textDocumentParams(sourceFile), 2),
		payload("shutdown", Json::nullValue, 3),
		payload("exit", Json::nullValue)
	});

	BOOST_CHECK(containsError(output, ErrorCode::RequestFailed, "Unknown file: file://" + sourceFile.generic_string()));
	BOOST_CHECK_EQUAL(output.find("\"code\":-32603"), std::string::npos);
	BOOST_CHECK_EQUAL(output.find("Unhandled exception"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(language_server_semantic_tokens_rejects_foreign_scheme_uri)
{
	util::TemporaryDirectory tempDir{"lsp-semantic-tokens-foreign-scheme-uri-test"};

	std::string output = runLanguageServer({
		payload("initialize", initializeParams(tempDir.path()), 1),
		payload("textDocument/semanticTokens/full", textDocumentParamsWithUri("https://example.invalid/main.hyp"), 2),
		payload("shutdown", Json::nullValue, 3),
		payload("exit", Json::nullValue)
	});

	BOOST_CHECK(containsError(output, ErrorCode::InvalidParams, "URI must start with file://"));
	BOOST_CHECK_EQUAL(output.find("\"code\":-32603"), std::string::npos);
	BOOST_CHECK_EQUAL(output.find("Unhandled exception"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(language_server_semantic_tokens_returns_empty_for_parse_errors)
{
	util::TemporaryDirectory tempDir{"lsp-semantic-tokens-parse-error-test"};
	boost::filesystem::path sourceFile = tempDir.path() / "main.hyp";

	std::string output = runLanguageServer({
		payload("initialize", initializeParams(tempDir.path()), 1),
		payload(
			"textDocument/didOpen",
			didOpenParams(sourceFile, "contract C { function f(")
		),
		payload("textDocument/semanticTokens/full", textDocumentParams(sourceFile), 2),
		payload("shutdown", Json::nullValue, 3),
		payload("exit", Json::nullValue)
	});

	BOOST_CHECK(output.find("\"id\":2") != std::string::npos);
	BOOST_CHECK(output.find("\"data\":[]") != std::string::npos);
	BOOST_CHECK_EQUAL(output.find("\"code\":-32603"), std::string::npos);
	BOOST_CHECK_EQUAL(output.find("Unhandled exception"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(language_server_did_change_requires_text_document)
{
	util::TemporaryDirectory tempDir{"lsp-did-change-missing-text-document-test"};
	Json::Value change;
	change["text"] = "contract C {}";
	Json::Value params;
	params["contentChanges"].append(std::move(change));

	std::string output = runLanguageServer({
		payload("initialize", initializeParams(tempDir.path()), 1),
		payload("textDocument/didChange", params),
		payload("shutdown", Json::nullValue, 2),
		payload("exit", Json::nullValue)
	});

	BOOST_CHECK(containsError(output, ErrorCode::RequestFailed, "Text document parameter missing."));
	BOOST_CHECK_EQUAL(output.find("\"code\":-32603"), std::string::npos);
	BOOST_CHECK_EQUAL(output.find("Unhandled exception"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(language_server_hover_returns_null_for_out_of_range_position)
{
	util::TemporaryDirectory tempDir{"lsp-hover-position-test"};
	boost::filesystem::path sourceFile = tempDir.path() / "main.hyp";

	std::string output = runLanguageServer({
		payload("initialize", initializeParams(tempDir.path()), 1),
		payload(
			"textDocument/didOpen",
			didOpenParams(sourceFile, "contract C { function f() public pure returns (uint) { return 1; } }")
		),
		payload("textDocument/hover", textDocumentPositionParams(sourceFile, 100, 0), 2),
		payload("shutdown", Json::nullValue, 3),
		payload("exit", Json::nullValue)
	});

	BOOST_CHECK(output.find("\"result\":null") != std::string::npos);
	BOOST_CHECK_EQUAL(output.find("\"code\":-32603"), std::string::npos);
	BOOST_CHECK_EQUAL(output.find("Unhandled exception"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(language_server_hover_handles_undocumented_identifier_path)
{
	util::TemporaryDirectory tempDir{"lsp-hover-identifier-path-test"};
	boost::filesystem::path sourceFile = tempDir.path() / "main.hyp";
	std::string const source =
		"library Lib { function f(uint self) internal pure returns (uint) { return self; } } "
		"contract C { using Lib for uint; }";
	size_t const hoverPosition = source.rfind("Lib");
	BOOST_REQUIRE_NE(hoverPosition, std::string::npos);

	std::string output = runLanguageServer({
		payload("initialize", initializeParams(tempDir.path()), 1),
		payload("textDocument/didOpen", didOpenParams(sourceFile, source)),
		payload("textDocument/hover", textDocumentPositionParams(sourceFile, 0, static_cast<int>(hoverPosition)), 2),
		payload("shutdown", Json::nullValue, 3),
		payload("exit", Json::nullValue)
	});

	BOOST_CHECK(output.find("\"result\":null") == std::string::npos);
	BOOST_CHECK_EQUAL(output.find("\"code\":-32603"), std::string::npos);
	BOOST_CHECK_EQUAL(output.find("Unhandled exception"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(language_server_positions_use_lsp_utf16_columns)
{
	util::TemporaryDirectory tempDir{"lsp-utf16-position-test"};
	boost::filesystem::path sourceFile = tempDir.path() / "main.hyp";
	std::string const source = "contract C { string s = unicode\"" "\xf0\x9f\x98\x83" "\"; uint x; }";
	std::string const sourceUnitName = sourceFile.generic_string();

	FileRepository repository(tempDir.path(), {});
	repository.setSourceByUri("file://" + sourceUnitName, source);

	size_t const uintByteOffset = source.find("uint");
	BOOST_REQUIRE_NE(uintByteOffset, std::string::npos);
	BOOST_CHECK_EQUAL(static_cast<int>(uintByteOffset), 39);
	BOOST_CHECK_EQUAL(byteOffsetToLSPLineColumn(source, static_cast<int>(uintByteOffset)).column, 37);

	Json::Value position;
	position["line"] = 0;
	position["character"] = 37;
	std::optional<langutil::SourceLocation> const location = parsePosition(repository, sourceUnitName, position);
	BOOST_REQUIRE(location);
	BOOST_CHECK_EQUAL(location->start, static_cast<int>(uintByteOffset));

	Json::Value range = sourceLocationToJsonRange(
		source,
		langutil::SourceLocation{
			static_cast<int>(uintByteOffset),
			static_cast<int>(uintByteOffset + 4),
			std::make_shared<std::string const>(sourceUnitName)
		}
	);
	BOOST_CHECK_EQUAL(range["start"]["character"].asInt(), 37);
	BOOST_CHECK_EQUAL(range["end"]["character"].asInt(), 41);
}

BOOST_AUTO_TEST_CASE(language_server_hover_rejects_malformed_position)
{
	util::TemporaryDirectory tempDir{"lsp-hover-malformed-position-test"};
	boost::filesystem::path sourceFile = tempDir.path() / "main.hyp";

	Json::Value hoverParams;
	hoverParams["textDocument"]["uri"] = "file://" + sourceFile.generic_string();
	hoverParams["position"]["line"] = 0;

	std::string output = runLanguageServer({
		payload("initialize", initializeParams(tempDir.path()), 1),
		payload("textDocument/didOpen", didOpenParams(sourceFile, "contract C {}")),
		payload("textDocument/hover", hoverParams, 2),
		payload("shutdown", Json::nullValue, 3),
		payload("exit", Json::nullValue)
	});

	BOOST_CHECK(containsError(output, ErrorCode::InvalidParams, "Invalid position parameter."));
	BOOST_CHECK_EQUAL(output.find("\"code\":-32603"), std::string::npos);
	BOOST_CHECK_EQUAL(output.find("Unhandled exception"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(language_server_rename_rejects_out_of_range_position)
{
	util::TemporaryDirectory tempDir{"lsp-rename-position-test"};
	boost::filesystem::path sourceFile = tempDir.path() / "main.hyp";

	std::string output = runLanguageServer({
		payload("initialize", initializeParams(tempDir.path()), 1),
		payload("textDocument/didOpen", didOpenParams(sourceFile, "contract C {}")),
		payload("textDocument/rename", renameParams(sourceFile, 100, 0, "D"), 2),
		payload("shutdown", Json::nullValue, 3),
		payload("exit", Json::nullValue)
	});

	BOOST_CHECK(containsError(output, ErrorCode::InvalidParams, "No symbol at requested position."));
	BOOST_CHECK_EQUAL(output.find("\"code\":-32603"), std::string::npos);
	BOOST_CHECK_EQUAL(output.find("Unhandled exception"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(language_server_rename_rejects_non_renameable_position)
{
	util::TemporaryDirectory tempDir{"lsp-rename-literal-test"};
	boost::filesystem::path sourceFile = tempDir.path() / "main.hyp";
	std::string const source = "contract C { function f() public pure returns (uint) { return 1; } }";

	std::string output = runLanguageServer({
		payload("initialize", initializeParams(tempDir.path()), 1),
		payload("textDocument/didOpen", didOpenParams(sourceFile, source)),
		payload("textDocument/rename", renameParams(sourceFile, 0, static_cast<int>(source.find('1')), "x"), 2),
		payload("shutdown", Json::nullValue, 3),
		payload("exit", Json::nullValue)
	});

	BOOST_CHECK(containsError(output, ErrorCode::InvalidParams, "No renameable symbol at requested position."));
	BOOST_CHECK_EQUAL(output.find("\"code\":-32603"), std::string::npos);
	BOOST_CHECK_EQUAL(output.find("Unhandled exception"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(language_server_rename_rejects_builtin_member_position)
{
	util::TemporaryDirectory tempDir{"lsp-rename-builtin-member-test"};
	boost::filesystem::path sourceFile = tempDir.path() / "main.hyp";
	std::string const source = "contract C { function f(bytes memory b) public pure returns (uint) { return b.length; } }";

	std::string output = runLanguageServer({
		payload("initialize", initializeParams(tempDir.path()), 1),
		payload("textDocument/didOpen", didOpenParams(sourceFile, source)),
		payload("textDocument/rename", renameParams(sourceFile, 0, static_cast<int>(source.find("length")), "size"), 2),
		payload("shutdown", Json::nullValue, 3),
		payload("exit", Json::nullValue)
	});

	BOOST_CHECK(containsError(output, ErrorCode::InvalidParams, "No renameable symbol at requested position."));
	BOOST_CHECK_EQUAL(output.find("\"code\":-32603"), std::string::npos);
	BOOST_CHECK_EQUAL(output.find("Unhandled exception"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(language_server_rename_rejects_magic_variable_position)
{
	util::TemporaryDirectory tempDir{"lsp-rename-magic-variable-test"};
	boost::filesystem::path sourceFile = tempDir.path() / "main.hyp";
	std::string const source = "contract C { function f() public view returns (uint) { return block.number; } }";

	std::string output = runLanguageServer({
		payload("initialize", initializeParams(tempDir.path()), 1),
		payload("textDocument/didOpen", didOpenParams(sourceFile, source)),
		payload("textDocument/rename", renameParams(sourceFile, 0, static_cast<int>(source.find("block")), "chain"), 2),
		payload("shutdown", Json::nullValue, 3),
		payload("exit", Json::nullValue)
	});

	BOOST_CHECK(containsError(output, ErrorCode::InvalidParams, "No renameable symbol at requested position."));
	BOOST_CHECK_EQUAL(output.find("\"code\":-32603"), std::string::npos);
	BOOST_CHECK_EQUAL(output.find("Unhandled exception"), std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()

}
