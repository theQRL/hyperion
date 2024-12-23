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
 * @date 2014
 * Public compiler API.
 */

#include <libhypc/libhypc.h>
#include <libhyperion/interface/StandardCompiler.h>
#include <libhyperion/interface/Version.h>
#include <libyul/YulString.h>

#include <cstdlib>
#include <list>
#include <string>

#include "license.h"

using namespace hyperion;
using namespace hyperion::util;

using hyperion::frontend::ReadCallback;
using hyperion::frontend::StandardCompiler;

namespace
{

// The std::strings in this list must not be resized after they have been added here (via hyperion_alloc()), because
// this may potentially change the pointer that was passed to the caller from hyperion_alloc().
static std::list<std::string> hyperionAllocations;

/// Find the equivalent to @p _data in the list of allocations of hyperion_alloc(),
/// removes it from the list and returns its value.
///
/// If any invalid argument is being passed, it is considered a programming error
/// on the caller-side and hence, will call abort() then.
std::string takeOverAllocation(char const* _data)
{
	for (auto iter = begin(hyperionAllocations); iter != end(hyperionAllocations); ++iter)
		if (iter->data() == _data)
		{
			std::string chunk = std::move(*iter);
			hyperionAllocations.erase(iter);
			return chunk;
		}

	abort();
}

/// Resizes a std::std::string to the proper length based on the occurrence of a zero terminator.
void truncateCString(std::string& _data)
{
	size_t pos = _data.find('\0');
	if (pos != std::string::npos)
		_data.resize(pos);
}

ReadCallback::Callback wrapReadCallback(CStyleReadFileCallback _readCallback, void* _readContext)
{
	ReadCallback::Callback readCallback;
	if (_readCallback)
	{
		readCallback = [=](std::string const& _kind, std::string const& _data)
		{
			char* contents_c = nullptr;
			char* error_c = nullptr;
			_readCallback(_readContext, _kind.data(), _data.data(), &contents_c, &error_c);
			ReadCallback::Result result;
			result.success = true;
			if (!contents_c && !error_c)
			{
				result.success = false;
				result.responseOrErrorMessage = "Callback not supported.";
			}
			if (contents_c)
			{
				result.success = true;
				result.responseOrErrorMessage = takeOverAllocation(contents_c);
			}
			if (error_c)
			{
				result.success = false;
				result.responseOrErrorMessage = takeOverAllocation(error_c);
			}
			truncateCString(result.responseOrErrorMessage);
			return result;
		};
	}
	return readCallback;
}

std::string compile(std::string _input, CStyleReadFileCallback _readCallback, void* _readContext)
{
	StandardCompiler compiler(wrapReadCallback(_readCallback, _readContext));
	return compiler.compile(std::move(_input));
}

}

extern "C"
{
extern char const* hyperion_license() noexcept
{
	static std::string fullLicenseText = otherLicenses + licenseText;
	return fullLicenseText.c_str();
}

extern char const* hyperion_version() noexcept
{
	return frontend::VersionString.c_str();
}

extern char* hyperion_compile(char const* _input, CStyleReadFileCallback _readCallback, void* _readContext) noexcept
{
	return hyperionAllocations.emplace_back(compile(_input, _readCallback, _readContext)).data();
}

extern char* hyperion_alloc(size_t _size) noexcept
{
	try
	{
		return hyperionAllocations.emplace_back(_size, '\0').data();
	}
	catch (...)
	{
		// most likely a std::bad_alloc(), if at all.
		return nullptr;
	}
}

extern void hyperion_free(char* _data) noexcept
{
	takeOverAllocation(_data);
}

extern void hyperion_reset() noexcept
{
	// This is called right before each compilation, but not at the end, so additional memory
	// can be freed here.
	yul::YulStringRepository::reset();
	hyperionAllocations.clear();
}
}
