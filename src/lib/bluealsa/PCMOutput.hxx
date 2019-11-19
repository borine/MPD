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

#ifndef MPD_BLUEALSA_PCMOUTPUT_HXX
#define MPD_BLUEALSA_PCMOUTPUT_HXX

#include "PCM.hxx"

#include "thread/Mutex.hxx"
#include "thread/Cond.hxx"

#include <exception>


class EventLoop;

namespace Bluealsa {

/**
 * Specializes a Bluealsa PCM so that it can be used with MPD's output
 * subsystem.
 */
class PCMOutput : PCM {

	size_t buffer_size;
	size_t buffer_period;

	/* protects #cond, #want_open and #pcm_error */
	mutable Mutex open_mutex;
	Cond cond;
	bool want_open = false;
	std::exception_ptr pcm_error;

public:

	PCMOutput(EventLoop &event_loop, const char *_address,
	                                         const char *suffix = nullptr)
		:PCM(event_loop, MODE_SOURCE, _address, suffix)
	{}

	virtual ~PCMOutput() noexcept {}

	/**
	 * Initialise the D-Bus context for this PCM.
	 * Must be called before any other methods.
	 */
	using PCM::Start;

	using PCM::Stop;
	using PCM::GetAddress;
	using PCM::GetAudioFormat;

	/**
	 * Request bluealsa to establish an A2DP transport with the device.
	 * Throws std::runtime_error if the device is not an A2DP sink,
	 * or if the request cannot be delivered.
	 */
	void Open();

	/**
	 * Close the A2DP transport with the device.
	 */
	void Close() {
		CloseTransport();
	}

	/**
	 * Handle bluealsa volume property change signals and allow to send
	 * volume change requests to bluealsa.
	 * Throws std::runtime_error if the device is not an A2DP sink,
	 * or if the request cannot be delivered.
	 */
	void EnableMixer();

	/**
	 * Stop handling bluealsa property change signals and stop sending
	 * volume change requests to bluealsa.
	 */
	void DisableMixer();

	void SetVolume(uint8_t volume);

	int8_t GetVolume() const noexcept;

	/**
	 * The capacity, in bytes, of the output pipe.
	 */
	size_t GetBufferSize() const noexcept {
		return buffer_size;
	}

	/**
	 * Write audio data to the output pipe. This method is non-blocking.
	 * The amount of data written is chosen to ensure that audio frames
	 * are never fragmented.
	 * Returns the number of bytes actually written.
	 * Throws a std::runtime_error if the system reports a write error.
	 */
	size_t Write(const void *buf, size_t count);

	/**
	 * Instruct the Bluetooth system to drain its buffers. Be aware that
	 * Bluetooth A2DP does not provide any explicit drain functionality, so
	 * it cannot be guaranteed that every frame will be played.
	 */
	void Drain() {
		control_fd.SendDrain(true);
	}

	/**
	 * Inform the Bluealsa that we have finished with this transport.
	 */
	void Drop() {
		control_fd.SendDrop(true);
	}

	/**
	 * Request the Bluealsa service to pause this transport.
	 */
	void Pause() {
		control_fd.SendPause(true);
	}

	/**
	 * Request the Bluealsa service to resume after a pause.
	 */
	void Resume() {
		control_fd.SendResume(true);
	}

private:

	void DeferredOpen() noexcept;

	void OnConfigurationComplete(std::exception_ptr error) noexcept override;

	void OnOpenComplete(std::exception_ptr error) noexcept override;

	void DoEnableMixer() noexcept;

};

} // namespace Bluealsa

#endif
