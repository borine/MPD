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


#include "PCMOutput.hxx"

#include "event/Call.hxx"
#include "event/DeferEvent.hxx"
#include "util/RuntimeError.hxx"

#include <stdexcept>

#include <linux/limits.h>  /* for PIPE_BUF */
#include <fcntl.h>

class EventLoop;

namespace Bluealsa {

static ssize_t SetPipeBufferSize(FileDescriptor fd, size_t size) {
	return fcntl(fd.Get(), F_SETPIPE_SZ, (int)size);
}

void
PCMOutput::Open()
{
	std::unique_lock<Mutex> lock(open_mutex);

	if (IsOpen())
		return;

	want_open = true;
	pcm_error = nullptr;

	DeferEvent defer_open(GetEventLoop(), BIND_THIS_METHOD(DeferredOpen));
	defer_open.Schedule();

	cond.wait(lock, [this]{ return !want_open; });

	if (pcm_error)
		std::rethrow_exception(pcm_error);

	/* Writes are performed in chunks of no more than PIPE_BUF so that
	 * we can be certain that audio frames are never fragmented */
	buffer_period = PIPE_BUF - (PIPE_BUF % GetAudioFormat().GetFrameSize());

	/* By default, the size of the pipe buffer is set to a too large value
	 * for our purpose. On modern Linux systems it is 65536 bytes.
	 * Large buffer in playback mode might contribute to an unnecessary
	 * audio delay.
	 * So we set the size of this buffer to 3 periods of audio, big enough
	 * to prevent audio tearing. Note, that the size will be rounded up by
	 * the kernel to the page size (typically 4096 bytes). */
	buffer_size = buffer_period * 3;
	buffer_size = SetPipeBufferSize(stream_fd, buffer_size);
}

void
PCMOutput::EnableMixer()
{
	BlockingCall(GetEventLoop(), [this](){ OpenMixer(); });
}

void
PCMOutput::DisableMixer()
{
	BlockingCall(GetEventLoop(), [this](){ CloseMixer(); });
}

void
PCMOutput::SetVolume(uint8_t vol)
{
	BlockingCall(GetEventLoop(), [this, vol](){ ChangeVolume(vol); });
}

int8_t
PCMOutput::GetVolume() const noexcept
{
	return ReadVolume();
}

size_t
PCMOutput::Write(const void *buf, size_t count) {

	if (count > buffer_period)
		count = buffer_period;

	ssize_t ret;
	while ((ret = stream_fd.Write(buf, count)) == -1) {
		switch (errno) {
			case EAGAIN:
				/* the pipe buffer is full */
				if (stream_fd.WaitWritable(500) == 0)
					throw std::runtime_error("Device timed out");
				/* pipe is now writeable - try again */
				continue;
			case EINTR:
				/* interrupted - try again */
				continue;
			case EPIPE:
				/* Bluealsa daemon has closed the pipe */
				CloseTransport();
				throw std::runtime_error("Device disconnected");
			default:
				/* other errors are fatal for this connection */
				throw FormatRuntimeError("Write error: %s",
				                              strerror(errno));
		}
	}
	return (size_t) ret;
}

void
PCMOutput::DeferredOpen() noexcept
try {
	Configure();
} catch (...) {
	const std::lock_guard<Mutex> lock(open_mutex);
	pcm_error = std::current_exception();
	want_open = false;
	cond.notify_all();
}

void
PCMOutput::OnConfigurationComplete(std::exception_ptr error) noexcept
try {
	if (error)
		std::rethrow_exception(error);

	OpenTransport();
} catch (...) {
	const std::lock_guard<Mutex> lock(open_mutex);
	pcm_error = std::current_exception();
	want_open = false;
	cond.notify_all();
}

void
PCMOutput::OnOpenComplete(std::exception_ptr error) noexcept
{
	const std::lock_guard<Mutex> lock(open_mutex);
	if (error)
		pcm_error = std::move(error);
	want_open = false;
	cond.notify_all();
}


} // namespace Bluealsa

