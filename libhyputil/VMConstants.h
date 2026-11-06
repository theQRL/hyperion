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
 * Constants defining the QRVM word size and related values.
 * Changing VMWordBytes from 32 to 64 is the single point of change
 * needed to switch the VM word size.
 */

#pragma once

#include <cstddef>

namespace hyperion
{

/// VM word size in bytes.
static constexpr unsigned VMWordBytes = 64;
/// VM word size in bits.
static constexpr unsigned VMWordBits = VMWordBytes * 8;
/// Alignment mask for rounding up to VM word boundary: (value + VMWordAlignmentMask) & ~VMWordAlignmentMask
static constexpr unsigned VMWordAlignmentMask = VMWordBytes - 1;

/// Width of a storage key on the chain. SSTORE and SLOAD address storage by the low 256 bits of
/// the 512-bit slot operand (go-qrl keys storage by a 32-byte hash), so a slot at or above 2**256
/// aliases its low 256 bits. The static storage layout and every hash-derived position stay below
/// 2**256; the code generators check the slots they derive from those with a run-time index or
/// length, see ArrayType::storageSlotsNeedRangeCheck().
static constexpr unsigned StorageKeyBits = 256;
/// Static arrays and structs of at least 2**StorageRangeCheckThresholdBits slots get that check
/// too: a hash-derived base can place a slot of theirs at or above 2**256 with a probability of
/// their size divided by 2**256. Smaller ones are not checked, since the probability is then no
/// higher than that of a hash collision and the check would cost gas on every access.
static constexpr unsigned StorageRangeCheckThresholdBits = 128;

/// QRL address width in bits: 512 (64 bytes), matching the go-qrllib
/// AddressSize after the 64-byte-address migration. Codegen masks,
/// integer-type cleanup, and the AddressType layout all derive from
/// this constant — equal to VMWordBits, i.e. the address fills the
/// entire VM word.
static constexpr unsigned AddressBits = 512;
/// QRL address width in bytes (derived from AddressBits).
static constexpr unsigned AddressBytes = AddressBits / 8;

}
