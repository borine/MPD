// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "StartDelayFilter.hxx"
#include "filter/NullFilter.hxx"
#include "filter/Prepared.hxx"
#include "pcm/AudioFormat.hxx"

#include <cstring>
#include <functional>
#include <random>

constexpr unsigned
get_sample_bits(const AudioFormat &format) noexcept
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
 * Fill the noise buffer with pseudo-random values.
 * This sound is not intended to be heard, only to inform the device that the
 * stream is active. So we optimize resource usage by generating a short
 * sequence representing a square wave of fixed amplitude and randomly varying
 * wavelength.
 */
void
StartDelayFilter::CreateNoise(const AudioFormat &format, std::uint8_t level) noexcept
{
	const unsigned sample_bits = get_sample_bits(format);
	const size_t sample_size = format.GetSampleSize();
	const uint8_t channels = format.channels;
	const std::size_t frame_size = sample_size * channels;
	const std::size_t n_frames = noise_buffer.size() / frame_size;

	/* amplitude of the generated wave.
	 * For integer formats with a sample size less than 16 bits the
	 * amplitude is always 1 bit (-48.1 dBFS for 8 bits);
	 * For larger integer formats and floats the level is scaled to the
	 * range -90.3 dBFS to -42.1 dBFS  */
	const std::int32_t amp = sample_bits >= 16
		? std::int32_t(level) << (sample_bits - 16)
		: 1;
	const float famp = float(level) / 32768.0f;

	/* generator of random boolean values */
	auto rng = std::bind(std::uniform_int_distribution<>(0, 1), std::default_random_engine());

	/* the address within the byte array to write the generated frame to */
	auto dest = noise_buffer.data();
	
	for (std::size_t i = 0; i < n_frames; i++, dest += sample_size) {

		/* vary the sign randomly */
        const bool negative = rng();

		/* write the value to the byte array */
		if (format.format == SampleFormat::FLOAT) {
			const float val = negative ? -famp : famp;
			std::memcpy(dest, &val, sizeof(val));
		}
		else {
			const int32_t val = negative ? -amp : amp;
			std::memcpy(dest, &val, sizeof(val));
		}

		/* copy this sample to each channel */
		for (auto c = 1; c < channels; c++, dest += sample_size)
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
		if (next_chunk_size > noise_buffer.size())
			next_chunk_size = noise_buffer.size();
		noise_bytes_read += next_chunk_size;
		return std::span{noise_buffer.begin(), noise_buffer.begin() + next_chunk_size};
	}

	return src;
}

std::span<const std::byte>
StartDelayFilter::ReadMore()
{
	if (noise_bytes_read < noise_bytes)
	{
		auto next_chunk_size = noise_bytes - noise_bytes_read;
		if (next_chunk_size > noise_buffer.size())
			next_chunk_size = noise_buffer.size();
		noise_bytes_read += next_chunk_size;
		return std::span{noise_buffer.begin(), noise_buffer.begin() + next_chunk_size};
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
