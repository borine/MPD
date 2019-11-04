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


#include "BluealsaInputPlugin.hxx"

#include "AudioFormat.hxx"
#include "config/Block.hxx"
#include "event/Loop.hxx"
#include "input/InputStream.hxx"
#include "input/CondHandler.hxx"
#include "lib/bluealsa/PCM.hxx"
#include "thread/Mutex.hxx"
#include "thread/Cond.hxx"
#include "event/Call.hxx"
#include "event/DeferEvent.hxx"
#include "util/StringBuffer.hxx"

#include <stdexcept>
#include <sys/ioctl.h>


static constexpr auto BLUEALSA_URI_PREFIX = "bluealsa://";

static struct {
	EventLoop *event_loop;
	std::string address;
	std::string suffix;
} global_config;

class BluealsaInputStream : Bluealsa::PCM, SocketMonitor, public InputStream {

	bool open = true;
	bool empty = true;

	mutable DeferEvent defer_monitor;
	std::exception_ptr pcm_error;

	struct Spec {
		std::string address;
		std::string suffix;
		explicit Spec(const char *uri) noexcept;
	};

	using InputStream::mutex;

public:

	/**
	 * Attempt to open the given URI.  Returns nullptr if the
	 * plugin does not support this URI.
	 *
	 * Throws std::runtime_error on error.
	 */
	static InputStreamPtr Open(const char *uri, Mutex &mutex);

	BluealsaInputStream(EventLoop &_loop, const char *_uri, Mutex &_mutex,
		                     const char *_address, const char *suffix)
		:Bluealsa::PCM(_loop, MODE_SINK, _address, suffix),
		 SocketMonitor(_loop),
		 InputStream(_uri, _mutex),
		 defer_monitor(_loop, BIND_THIS_METHOD(DeferredMonitor))
	{}

	~BluealsaInputStream() noexcept override {
		defer_monitor.Cancel();
		BlockingCall(defer_monitor.GetEventLoop(),
		                                       [this]() { Cancel(); });
	}

	/**
	 * Request bluealsa to establish an a2dp transport with the device.
	 * Throws std::runtime_error if the request cannot be delivered.
	 */
	void StartRequest();

	/* Virtual methods from InputStream */

	size_t Read(std::unique_lock<Mutex> &lock,
			    void *ptr, size_t count) override;

	void Check() override {
		if (pcm_error)
			std::rethrow_exception(std::exchange(pcm_error,
			                               std::exception_ptr()));
	}

	bool IsEOF() const noexcept override {
		return !open;
	}

	bool IsAvailable() const noexcept override {
		if (pcm_error || IsEOF() || UnreadBytes() > 0)
			return true;
		defer_monitor.Schedule();
		return false;
	}

private:

	size_t UnreadBytes() const noexcept {
		int count = 0;
		ioctl(stream_fd.Get(), FIONREAD, &count);
		return (unsigned) count;
	}

	void DeferredMonitor() noexcept {
		ScheduleRead();
	}

	bool OnSocketReady(unsigned flags) noexcept;

	void DeferredOpen() noexcept;

	void OnConfigurationComplete(std::exception_ptr error) noexcept override;

	void OnOpenComplete(std::exception_ptr error) noexcept override;
};


BluealsaInputStream::Spec::Spec(const char *uri) noexcept {

	assert(StringStartsWithIgnoreCase(uri, BLUEALSA_URI_PREFIX));

	uri = StringAfterPrefixIgnoreCase(uri, BLUEALSA_URI_PREFIX);
	if (*uri == 0) {
		address = global_config.address;
		suffix = global_config.suffix;
		return;
	}
	const char *slash = StringFind(uri, '/');
	if (slash == nullptr) {
		address = uri;
		suffix = global_config.suffix;
	}
	else if (slash == uri) {
		address = global_config.address;
		suffix = uri;
	}
	else {
		address.assign(uri, slash - uri);
		suffix.assign(slash + 1);
	}
}

InputStreamPtr
BluealsaInputStream::Open(const char *uri, Mutex &mutex)
{
	Spec spec(uri);
	auto s = std::make_unique<BluealsaInputStream>(
	                            *global_config.event_loop, uri, mutex,
	                            spec.address.c_str(), spec.suffix.c_str());
	s->StartRequest();
	return s;
}

void
BluealsaInputStream::StartRequest()
{
	if (IsOpen())
		return;

	PCM::Start();
	BlockingCall(PCM::GetEventLoop(), BIND_THIS_METHOD(DeferredOpen));
}

size_t
BluealsaInputStream::Read(std::unique_lock<Mutex> &lock, void *ptr,
                                                                  size_t count)
{
	assert(!defer_monitor.GetEventLoop().IsInside());

	CondInputStreamHandler cond_handler;
	size_t bytes_in_pipe;

	/* wait for data */
	while (true) {
		Check();

		if (IsEOF())
			return 0;

		if (!empty) {
			bytes_in_pipe = UnreadBytes();
			if (bytes_in_pipe > 0 || IsEOF())
				break;
		}

		const ScopeExchangeInputStreamHandler h(*this, &cond_handler);
		empty = true;
		defer_monitor.Schedule();
		cond_handler.cond.wait(lock);
	}

	const size_t nbytes = std::min(count, bytes_in_pipe);
	auto bytes_read = stream_fd.Read(ptr, nbytes);
	if (bytes_read < 0)
		throw std::runtime_error("Read from pipe failed");

	assert((size_t)bytes_read == nbytes);

	AddOffset(bytes_read);

	return bytes_read;
}

bool
BluealsaInputStream::OnSocketReady(unsigned flags) noexcept
{
	const std::lock_guard<Mutex> protect(mutex);
	if (flags & READ)
		empty = false;
	if (flags & HANGUP)
		open = false;
	try {
		if (flags & ERROR)
			throw std::runtime_error("pipe read error");
	} catch (...) {
		pcm_error = std::current_exception();
	}
	Cancel();
	if (ready)
		InvokeOnAvailable();
	else
		InvokeOnReady();
	return open;
}

void
BluealsaInputStream::DeferredOpen() noexcept
try {
	Configure();
} catch (...) {
	const std::lock_guard<Mutex> lock(mutex);
	pcm_error = std::current_exception();
	SetReady();
}

void
BluealsaInputStream::OnConfigurationComplete(std::exception_ptr error) noexcept
try {
	if (error)
		std::rethrow_exception(error);

	Bluealsa::PCM::OpenTransport();
} catch (...) {
	const std::lock_guard<Mutex> lock(mutex);
	pcm_error = std::current_exception();
	SetReady();
}

void
BluealsaInputStream::OnOpenComplete(std::exception_ptr error) noexcept
{
	const std::lock_guard<Mutex> lock(mutex);
	if (error)
		pcm_error = std::move(error);
	std::string mimestr = "audio/x-mpd-alsa-pcm;format=";
	mimestr += ToString(GetAudioFormat());
	SetMimeType(mimestr.c_str());
	SocketMonitor::Open(
	              SocketDescriptor::FromFileDescriptor(stream_fd));
	SetReady();
}


static void
bluealsa_input_init(EventLoop &event_loop, const ConfigBlock &block)
{
	global_config.event_loop = &event_loop;
	global_config.address = block.GetBlockValue("default_address", "");
	global_config.suffix = block.GetBlockValue("default_dbus_suffix", "");
}


static constexpr const char *bluealsa_prefixes[] = {
	BLUEALSA_URI_PREFIX,
	nullptr,
};

const struct InputPlugin input_plugin_bluealsa = {
	"bluealsa",
	bluealsa_prefixes,
	bluealsa_input_init,
	nullptr,
	BluealsaInputStream::Open,
	nullptr,
};
