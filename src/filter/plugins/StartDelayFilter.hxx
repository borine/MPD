// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#pragma once

#include "filter/Filter.hxx"

#include <chrono>

static const std::size_t NOISE_BUFFER_BYTES = 200 * 48 * 2 * 2;

/**
 * A #Filter to insert a preliminary burst of noise or silence at the
 * start of a stream. Useful for use with output devices that drop initial
 * audio samples whilst performing protocol handshakes or powering up idle
 * circuits (for example some HDMI devices).
 */
class StartDelayFilter final : public Filter {

	std::size_t noise_bytes;
	std::byte noise_buffer[NOISE_BUFFER_BYTES];
	std::span<const std::byte> initial_data;
	std::size_t noise_bytes_read;
	bool on_seek;

public:
	/**
	 * @param time the duration of the delay
	 * @param level the loudness of the noise
	 */
	StartDelayFilter(const AudioFormat &audio_format,
			 std::chrono::milliseconds time,
			 std::uint8_t level,
			 bool on_seek) noexcept;

	/* virtual methods from class Filter */
	std::span<const std::byte> FilterPCM(std::span<const std::byte> src) override;
	std::span<const std::byte> ReadMore() override;
	std::span<const std::byte> Flush() override;
	void Reset() noexcept override;

private:
	void CreateNoise(SampleFormat format, std::uint8_t level) noexcept;

};
