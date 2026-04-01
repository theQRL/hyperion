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
 * Compiler that transforms Yul Objects to QRVM bytecode objects.
 */

#pragma once

#include <optional>
#include <cstdint>

namespace hyperion::yul
{
struct Object;
class AbstractAssembly;
struct QRVMDialect;

class QRVMObjectCompiler
{
public:
	static void compile(
		Object& _object,
		AbstractAssembly& _assembly,
		QRVMDialect const& _dialect,
		bool _optimize
	);
private:
	QRVMObjectCompiler(AbstractAssembly& _assembly, QRVMDialect const& _dialect):
		m_assembly(_assembly), m_dialect(_dialect)
	{}

	void run(Object& _object, bool _optimize);

	AbstractAssembly& m_assembly;
	QRVMDialect const& m_dialect;
};

}
