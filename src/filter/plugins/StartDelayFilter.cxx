// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "StartDelayFilter.hxx"
#include "filter/NullFilter.hxx"
#include "filter/Prepared.hxx"
#include "pcm/AudioFormat.hxx"

#include <cstring>

static unsigned
GetSampleBits(const AudioFormat &format) noexcept
{
	if (format.format == SampleFormat::S24_P32)
		return 24;
	return format.GetSampleSize() * CHAR_BIT;
}

StartDelayFilter::StartDelayFilter(const AudioFormat &audio_format,
			 std::chrono::milliseconds time,
			 std::uint8_t level,
			 bool _on_seek) noexcept
				:Filter(audio_format),
				 noise_bytes(audio_format.TimeToSize(time)),
				 noise_buffer{},
				 initial_data(),
				 noise_bytes_read(0),
				 on_seek(_on_seek)
{
	if (level > 0)
		CreateNoise(audio_format, level);
}

/**
 * Fill a buffer with pseudo-random values.
 * Because this sound is not intended to be heard, only to inform the
 * device that the stream is active, we do not care about the uniformity of
 * the random value distribution nor the sequence length. So to optimize
 * resource usage we generate a short sequence representing a square wave of
 * fixed amplitude and randomly varying wavelength.
 */
void
StartDelayFilter::CreateNoise(const AudioFormat &format, std::uint8_t level) noexcept
{
	const unsigned sample_bits = GetSampleBits(format);
	const size_t sample_size = format.GetSampleSize();
	const uint8_t channels = format.channels;
	const std::size_t frame_size = sample_size * channels;
	const std::size_t n_frames = NOISE_BUFFER_BYTES / frame_size;

	/* amplitude in LSBs of the native sample width */
	const std::int32_t amp = sample_bits >= 16
		? std::int32_t(level) << (sample_bits - 16)
		: 1;
	const float famp = float(level) / 32768.0f;

	auto *dest = noise_buffer;
	std::uint32_t rng = 0x9E3779B9u;
	for (std::size_t i = 0; i < n_frames; ++i, dest += sample_size) {

		/* vary the sign randomly */
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		const bool negative = (rng >> 16) & 1;

		/* create a 32-bit representation of the sample bits */
		std::uint32_t u;
		if (format.format == SampleFormat::FLOAT) {
			const float f = negative ? -famp : famp;
			static_assert(sizeof(f) == sizeof(u));
			std::memcpy(&u, &f, sizeof(u));
		} else {
			u = std::uint32_t(negative ? -amp : amp);
		}

		/* store the low "bytes" bytes of the (sign-extended)
		   value into the noise buffer in native byte order */
		for (unsigned b = 0; b < sample_size; ++b)
			if constexpr (std::endian::native == std::endian::little)
				dest[b] = std::byte(u >> (8 * b));
			else
				dest[sample_size - 1 - b] = std::byte(u >> (8 * b));

		/* copy this sample to each channel */
		for (size_t s = 1; s < channels; s++, dest += sample_size)
			std::memcpy(dest + sample_size, dest, sample_size);

	}
}

std::span<const std::byte>
StartDelayFilter::FilterPCM(std::span<const std::byte> src)
{
	if (noise_bytes_read < noise_bytes)
	{
		initial_data = src;
		auto next_chunk_size = noise_bytes - noise_bytes_read;
		if (next_chunk_size > NOISE_BUFFER_BYTES)
			next_chunk_size = NOISE_BUFFER_BYTES;
		noise_bytes_read += next_chunk_size;
		return std::span{ noise_buffer, next_chunk_size };
	}

	return src;
}

std::span<const std::byte>
StartDelayFilter::ReadMore()
{
	if (noise_bytes_read < noise_bytes)
	{
		auto next_chunk_size = noise_bytes - noise_bytes_read;
		if (next_chunk_size > NOISE_BUFFER_BYTES)
			next_chunk_size = NOISE_BUFFER_BYTES;
		noise_bytes_read += next_chunk_size;
		return std::span{ noise_buffer, next_chunk_size };
	}

	if (initial_data.size() != 0)
	{
		std::span<const std::byte> res = initial_data;
		initial_data = {};
		return res;
	}

	return {};
}

std::span<const std::byte>
StartDelayFilter::Flush()
{
	return ReadMore();
}

void
StartDelayFilter::Reset() noexcept
{
	if (on_seek)
	{
		initial_data = {};
		noise_bytes_read = 0;
	}
}
