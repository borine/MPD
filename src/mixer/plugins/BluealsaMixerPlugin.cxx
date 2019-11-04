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

#include "output/plugins/BluealsaOutputPlugin.hxx"
#include "mixer/MixerInternal.hxx"

class BluealsaMixer final : public Mixer {

	BluealsaOutput &output;

public:
	BluealsaMixer(BluealsaOutput &_output, MixerListener &_listener)
		:Mixer(bluealsa_mixer_plugin, _listener),
		 output(_output)
	{}

	virtual ~BluealsaMixer() {}

	/* virtual methods from class Mixer */

	void Open() override {}

	void Close() noexcept override {}

	int GetVolume() override {
		return bluealsa_output_get_volume(output);
	}

	void SetVolume(unsigned volume) {
		bluealsa_output_set_volume(output, volume);
	}
};

static Mixer *
bluealsa_mixer_init(EventLoop &, AudioOutput &ao,
		       MixerListener &listener,
		       const ConfigBlock &)
{
	BluealsaOutput &bo = (BluealsaOutput &)ao;
	BluealsaMixer *bm = new BluealsaMixer(bo, listener);
	return bm;
}

const MixerPlugin bluealsa_mixer_plugin = {
	bluealsa_mixer_init,
	false,
};
