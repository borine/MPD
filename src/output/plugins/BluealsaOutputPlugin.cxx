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


#include "BluealsaOutputPlugin.hxx"
#include "../OutputAPI.hxx"
#include "lib/bluealsa/PCMOutput.hxx"
#include "Log.hxx"
#include "mixer/MixerList.hxx"
#include "pcm/Export.hxx"
#include "util/Domain.hxx"
#include "util/Manual.hxx"

#include <atomic>

class EventLoop;


class BluealsaOutput final : AudioOutput {

	bool use_mixer = false;
	Bluealsa::PCMOutput sink;
	bool paused = false;

	Manual<PcmExport> pcm_export;
	size_t in_frame_size;
	size_t out_frame_size;

public:

	static AudioOutput *Create(EventLoop &eventloop,
				   const ConfigBlock &block) {
		return new BluealsaOutput(eventloop, block);
	}

	virtual ~BluealsaOutput() {}

	void Enable() override {
		sink.Start();
		if (use_mixer)
			sink.EnableMixer();

		pcm_export.Construct();
	}

	void Disable() noexcept override {
		pcm_export.Destruct();
		if (use_mixer)
			sink.DisableMixer();
		sink.Stop();
	}

	void Open(AudioFormat &audio_format) override;
	void Close() noexcept override;

	std::chrono::steady_clock::duration Delay() const noexcept override;
	size_t Play(const void *chunk, size_t size) override;
	void Drain() override;
	void Cancel() noexcept override;
	bool Pause() override;

	void OpenMixer() noexcept;
	void CloseMixer() noexcept;
	int GetVolume() const noexcept;
	void SetVolume(int8_t volume);

private:

	explicit BluealsaOutput(EventLoop &eventloop, const ConfigBlock &block)
		:AudioOutput(FLAG_ENABLE_DISABLE|FLAG_PAUSE),
		 use_mixer(StringIsEqual(block.GetBlockValue("mixer_type",
		                                      "hardware"), "hardware")),
		 sink(eventloop, block.GetBlockValue("device", nullptr),
			             block.GetBlockValue("suffix", nullptr))
	{}
};

void
BluealsaOutput::Open(AudioFormat &audio_format)
{
	sink.Open();

	FormatDebug(Domain("bluealsa"), "opened device %s", sink.GetAddress());

	auto transport_format = sink.GetAudioFormat();
	PcmExport::Params params;
	params.pack24 = transport_format.packed;
	params.reverse_endian = transport_format.reverse_endian;

	pcm_export->Open(transport_format.format, transport_format.channels,
	                                                               params);
	in_frame_size = audio_format.GetFrameSize();
	out_frame_size = pcm_export->GetOutputFrameSize();

	audio_format = transport_format;
}

void
BluealsaOutput::Close() noexcept
{
	sink.Close();
}

std::chrono::steady_clock::duration
BluealsaOutput::Delay() const noexcept
{
	return paused
		? std::chrono::milliseconds(500)
		: std::chrono::steady_clock::duration::zero();
}

size_t
BluealsaOutput::Play(const void *chunk, size_t size)
{
	if (paused) {
		sink.Resume();
		paused = false;
	}

	const auto e = pcm_export->Export({chunk, size});
	if (e.empty())
		return size;

	size_t bytes_written = sink.Write((const uint8_t *)e.data, e.size);
	return pcm_export->CalcInputSize(bytes_written);
}

void BluealsaOutput::Drain()
{
	sink.Drain();
}

void
BluealsaOutput::Cancel() noexcept
try {
	pcm_export->Reset();
	sink.Drop();
} catch(...) {}

bool
BluealsaOutput::Pause()
{
	if (paused)
		return true;

	try {
		sink.Pause();
	} catch (...) {
		return false;
	}
	paused = true;
	return true;
}

int
BluealsaOutput::GetVolume() const noexcept
{
	return use_mixer ? sink.GetVolume() : -1;
}

void
BluealsaOutput::SetVolume(int8_t volume)
{
	if (use_mixer)
		sink.SetVolume(volume);
}

void
bluealsa_output_set_volume(BluealsaOutput &output, unsigned volume)
{
	output.SetVolume(volume);
}

int
bluealsa_output_get_volume(BluealsaOutput &output)
{
	return output.GetVolume();
}

const struct AudioOutputPlugin bluealsa_output_plugin = {
	"bluealsa",
	nullptr,
	BluealsaOutput::Create,
	&bluealsa_mixer_plugin,
};

