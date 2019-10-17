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

#include "NonBlock.hxx"
#include "event/MultiSocketMonitor.hxx"
#include "util/RuntimeError.hxx"

std::chrono::steady_clock::duration
AlsaNonBlockPcm::PrepareSockets(MultiSocketMonitor &m, snd_pcm_t *pcm)
{
	count = snd_pcm_poll_descriptors_count(pcm);
	if (count <= 0) {
		if (count == 0)
			throw std::runtime_error("snd_pcm_poll_descriptors_count() failed");
		else
			throw FormatRuntimeError("snd_pcm_poll_descriptors_count() failed: %s",
						 snd_strerror(-count));
	}

	pfds = pfd_buffer.Get(count);

	count = snd_pcm_poll_descriptors(pcm, pfds, count);
	if (count <= 0) {
		if (count == 0)
			throw std::runtime_error("snd_pcm_poll_descriptors() failed");
		else
			throw FormatRuntimeError("snd_pcm_poll_descriptors() failed: %s",
						 snd_strerror(-count));
	}

	m.ReplaceSocketList(pfds, count);
	return std::chrono::steady_clock::duration(-1);
}

bool
AlsaNonBlockPcm::DispatchSockets(MultiSocketMonitor &m,
				 snd_pcm_t *pcm)
{
	m.ForEachReturnedEvent([this](SocketDescriptor s, unsigned events){
			for (auto i = pfds; i < pfds + count; i++)
				if (i->fd == s.Get()) {
					i->revents = events;
					break;
				}
		});

	unsigned short revents;
	int err = snd_pcm_poll_descriptors_revents(pcm, pfds, count, &revents);
	if (err < 0 && err != -EPIPE && err != -ESTRPIPE)
		throw FormatRuntimeError("snd_pcm_poll_descriptors_revents() failed: %s",
					 snd_strerror(-err));

	return revents != 0;
}

std::chrono::steady_clock::duration
AlsaNonBlockMixer::PrepareSockets(MultiSocketMonitor &m, snd_mixer_t *mixer) noexcept
{
	count = snd_mixer_poll_descriptors_count(mixer);
	if (count <= 0) {
		m.ClearSocketList();
		return std::chrono::steady_clock::duration(-1);
	}

	pfds = pfd_buffer.Get(count);

	count = snd_mixer_poll_descriptors(mixer, pfds, count);
	if (count < 0)
		count = 0;

	m.ReplaceSocketList(pfds, count);
	return std::chrono::steady_clock::duration(-1);
}

bool
AlsaNonBlockMixer::DispatchSockets(MultiSocketMonitor &m,
				   snd_mixer_t *mixer)
{
	m.ForEachReturnedEvent([this](SocketDescriptor s, unsigned events){
			for (auto i = pfds; i < pfds + count; i++)
				if (i->fd == s.Get()) {
					i->revents = events;
					break;
				}
		});

	unsigned short revents;
	int err = snd_mixer_poll_descriptors_revents(mixer, pfds, count, &revents);
	if (err < 0)
		throw FormatRuntimeError("snd_mixer_poll_descriptors_revents() failed: %s",
					 snd_strerror(-err));

	return revents != 0;
}
