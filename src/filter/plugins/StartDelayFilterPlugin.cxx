// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "StartDelayFilterPlugin.hxx"
#include "StartDelayFilter.hxx"
#include "filter/FilterPlugin.hxx"
#include "filter/NullFilter.hxx"
#include "filter/Filter.hxx"
#include "filter/Prepared.hxx"
#include "config/Block.hxx"

static constexpr unsigned START_DELAY_DEFAULT_MS = 1250;

/** amplitude of the noise, in LSBs of a 16 bit sample */
static constexpr unsigned START_DELAY_NOISE_LEVEL_DEFAULT = 1;
static constexpr unsigned START_DELAY_NOISE_LEVEL_MAX = 255;

[[gnu::pure]]
static bool
FormatSupported(const AudioFormat &audio_format) noexcept
{
	return audio_format.format != SampleFormat::DSD &&
		 audio_format.format != SampleFormat::UNDEFINED;
}

class PreparedStartDelayFilter final : public PreparedFilter {
	std::chrono::milliseconds time;
	unsigned int level;
	bool on_seek;

public:
	explicit PreparedStartDelayFilter(std::chrono::milliseconds _time, unsigned int _level, bool _on_seek) noexcept
		:time(_time),
		 level(_level),
		 on_seek(_on_seek)
		{}

	/* virtual methods from class PreparedFilter */
	std::unique_ptr<Filter> Open(AudioFormat &af) override;
};

std::unique_ptr<Filter>
PreparedStartDelayFilter::Open(AudioFormat &audio_format)
{
	if ((FormatSupported(audio_format) || level == 0) && time.count() > 0)
		return std::make_unique<StartDelayFilter>(audio_format,
					      time,
					      level,
					      on_seek);
	else
		/* Unable to generate noise in the required format,
		   so let's copy as-is using NullFilter */
		return std::make_unique<NullFilter>(audio_format);
}

static std::unique_ptr<PreparedFilter>
start_delay_filter_init(const ConfigBlock &block)
{
	unsigned int time = block.GetBlockValue("delay_ms", START_DELAY_DEFAULT_MS);
	unsigned int level = block.GetBlockValue("noise_level", START_DELAY_NOISE_LEVEL_DEFAULT);
	if (level > START_DELAY_NOISE_LEVEL_MAX)
		throw std::runtime_error("level out of range");
	bool on_seek = block.GetBlockValue("on_seek", false);

	return std::make_unique<PreparedStartDelayFilter>(std::chrono::milliseconds(time), level, on_seek);
}

const FilterPlugin start_delay_filter_plugin = {
	"startdelay",
	start_delay_filter_init,
};
