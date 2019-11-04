/*
 * Copyright 2003-2019 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef MPD_BLUEALSA_FORMAT_HXX
#define MPD_BLUEALSA_FORMAT_HXX

#include "AudioFormat.hxx"
#include "util/ByteOrder.hxx"

#include <cassert>
#include <stdexcept>

namespace Bluealsa {


/**
 * Converts between Bluealsa's D-Bus PCM format description and MPD's
 * internal AudioFormat description.
 */
struct TransportFormat : public AudioFormat {

	static constexpr uint16_t SIGN_MASK = 0x8000;
	static constexpr uint16_t ENDIAN_MASK = 0x4000;
	static constexpr uint16_t BITWIDTH_MASK = 0x3FFF;

	bool reverse_endian;
	bool packed;

	/**
	 * Earlier versions of the Bluealsa D-Bud API did not implement
	 * the Format property - instead the sample format was always
	 * signed 16-bit little-endian. To accomodate those versions,
	 * the default constructor sets format to S16
	 */
	constexpr TransportFormat() :
		AudioFormat(0, SampleFormat::S16, 0),
		reverse_endian(!IsLittleEndian()),
		packed(false)
	{}

	static constexpr TransportFormat Undefined() {
		return TransportFormat();
	}

	void DecodeSampleFormat(uint16_t encoded_format) {
		/* The highest two bits of the 16-bit integer determine
		   the signedness and the endianness respectively.
		   The remaining bits store the bit-width. */

		const bool is_signed = encoded_format & SIGN_MASK;
		if (!is_signed)
			throw std::runtime_error("Unsigned sample format not supported");

		reverse_endian = (encoded_format & ENDIAN_MASK) &&
		                                              IsLittleEndian();

		const uint8_t bitwidth =
		                    (uint8_t) (encoded_format & BITWIDTH_MASK);
		switch (bitwidth) {
			case 8:
				format = SampleFormat::S8;
				break;
			case 16:
				format = SampleFormat::S16;
				break;
			case 24:
				format = SampleFormat::S24_P32;
				packed = true;
				break;
			case 32:
				format = SampleFormat::S32;
				break;
			default:
				throw std::runtime_error("Unsupported sample format");
		}
	}

	uint16_t EncodeSampleFormat() const noexcept {
		/* MPD does not support unsigned formats */
		uint16_t encoded_format = SIGN_MASK;

		switch (format) {
			case SampleFormat::S8:
				encoded_format += 8;
				break;
			case SampleFormat::S16:
				encoded_format += 16;
				break;
			case SampleFormat::S24_P32:
				encoded_format += 24;
				/* TODO also need to encode packed property
				 * here if the bluealsa spec is extended to
				 * support it */
				break;
			case SampleFormat::S32:
				encoded_format += 32;
				break;
			default:
				assert(false);
				gcc_unreachable();
		}

		if (reverse_endian && IsLittleEndian())
			encoded_format |= ENDIAN_MASK;

		return encoded_format;
	}

	unsigned GetSampleSize() const {
		return packed ? 24 : sample_format_size(format);
	}


};

} // namespace Bluealsa

#endif
