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

#ifndef MPD_BLUEALSA_PCM_HXX
#define MPD_BLUEALSA_PCM_HXX

#include "ControlSocket.hxx"
#include "Format.hxx"
#include "Service.hxx"

#include "lib/dbus/AsyncRequest.hxx"
#include "lib/dbus/Message.hxx"
#include "lib/dbus/ReadIter.hxx"
#include "lib/dbus/ScopeMatch.hxx"
#include "system/FileDescriptor.hxx"
#include "thread/Mutex.hxx"

#include <exception>
#include <string>

class EventLoop;

namespace Bluealsa {


static constexpr auto MANAGER_INTERFACE = "org.bluealsa.Manager1";
static constexpr auto PCM_INTERFACE = "org.bluealsa.PCM1";

/**
 * A D-Bus "proxy" class implementing the Bluealsa "PCM" interface
 */
class PCM {

public:

	enum Mode : uint8_t {
		MODE_UNKNOWN = 0,
		MODE_SINK = 1,
		MODE_SOURCE = 2,
		MODE_BOTH = 3,
	};

private:

	static constexpr auto vol_match_string = "type='signal',"
	                          "interface='" DBUS_INTERFACE_PROPERTIES "',"
	                          "member='PropertiesChanged',"
	                          "arg0='org.bluealsa.PCM1',"
	                          "sender='";

	/* D-Bus bluealsa service hosting this PCM */
	Service service;

	/* The D-Bus object path for this PCM */
	std::string object_path;

	/* Bluetooth address as encoded in the above object_path */
	std::string pattern;

	/* Audio format negotiated between host and device when connected */
	TransportFormat format;

	/* Bit field indicating supported modes (sink/source/both) */
	uint8_t supported_modes;

	/* Mode selected by client */
	Mode selected_mode;

	/* Mixer Volume */
	int8_t volume;

	/* protects #volume #mixer_open and #volume_match */
	mutable Mutex mutex;

	ODBus::AsyncRequest dbus_open_request;
	ODBus::AsyncRequest dbus_volume_request;
	bool mixer_open = false;
	Manual<ODBus::ScopeMatch> volume_match;
	std::string vol_match_spec;

protected:

	/* Bluetooth address of the device */
	std::string address;

	/* Descriptor for sending/receiving PCM samples */
	FileDescriptor stream_fd;

	 /* Socket for bluealsa control messages */
	ControlSocket control_fd;

public:

	PCM(EventLoop &event_loop, Mode mode, const char *_address,
	                                         const char *suffix = nullptr)
		:service(event_loop, suffix),
		 pattern(_address ? _address : ""),
		 supported_modes(MODE_UNKNOWN),
		 selected_mode(mode),
		 volume(-1),
		 vol_match_spec(std::string(vol_match_string) +
		                              service.Name() + "'"),
		                              address(_address ? _address : "")
	{
		/* encode the address into the pattern - used for matching the
		   object path. */
		std::replace(pattern.begin(), pattern.end(), ':', '_');
	}

	virtual ~PCM() noexcept;

	/**
	 * The EventLoop to be used for sending D-Bus messages to this PCM.
	 */
	EventLoop &GetEventLoop() noexcept {
		return service.GetEventLoop();
	}

	/**
	 * Initiate the D-Bus Bluealsa context.
	 * Must be called before any other methods.
	 */
	void Start() {
		service.Start();
	}

	/**
	 * Release the D-Bus Bluealsa context.
	 */
	void Stop() {
		service.Stop();
	}

	/**
	 * Request bluealsa to fetch the configuration parameters for an A2DP
	 * transport with the device.
	 * Throws std::runtime_error if the request cannot be delivered.
	 */
	void Configure();

	/**
	 * Request bluealsa to establish an A2DP transport with the device.
	 * Throws std::runtime_error if the request cannot be delivered.
	 */
	void OpenTransport();

	/**
	 * Closes the a2dp transport or cancels any in-progress OpenTransport()
	 * request.
	 * This method is synchronous - the audio stream is immediately
	 * terminated.
	 */
	void CloseTransport() noexcept;

	/**
	 * Request bluealsa to control the output volume to the device.
	 * Throws std::runtime_error if the request cannot be delivered.
	 */
	void OpenMixer();

	/**
	 * Stop controlling and watching the bluealsa volume for the device.
	 * Throws std::runtime_error if the request cannot be delivered.
	 */
	void CloseMixer();

	/**
	 * Request bluealsa to change the output volume to the device.
	 * Throws std::runtime_error if the request cannot be delivered.
	 */
	void ChangeVolume(uint8_t volume);

	/**
	 * Returns the latest volume reported by bluealsa,
	 * or -1 if the volume is unknown.
	 */
	int8_t ReadVolume() const noexcept {
		std::lock_guard<Mutex> lock(mutex);
		return volume;
	}

	/**
	 * The bluetooth address of the associated device
	 */
	const char *GetAddress() const noexcept {
		return address.c_str();
	}

	/**
	 * If #IsValid(), returns the audio format required by this PCM.
	 * Otherwise returns an invalid format.
	 */
	TransportFormat GetAudioFormat() const noexcept {
		return format;
	}

	/**
	 * Has #Configure() successfully populated this object ?
	 */
	bool IsValid() const noexcept;

	/**
	 * Has #OpenTransport() successfully opened the audio stream?
	 */
	bool IsOpen() const noexcept;

protected:

	/**
	 * Called in the D-Bus EventLoop thread when a #Configure() request
	 * completes.
	 * #error is nullptr if configuration succeeded.
	 */
	virtual void OnConfigurationComplete(std::exception_ptr error) noexcept = 0;

	/**
	 * Called in the D-Bus EventLoop thread when an #OpenTransport() request
	 * completes.
	 * #error is nullptr if the open succeeded.
	 */
	virtual void OnOpenComplete(std::exception_ptr error) noexcept = 0;


private:

	bool MatchPath(const char *test_path) noexcept;

	void Reset() noexcept;

	void Populate(const char *name, ODBus::ReadMessageIter &&i);

	bool Match(ODBus::ReadMessageIter &&i);

	/**
	 * Parse a list of bluealsa PCM interfaces, as returned by a call to
	 * "GetPCMs()" on the bluealsa Manager interface. Populate this
	 * instance if matching object path found.
	 * Throws std::runtime_error if #i is invalid.
	 */
	void ParsePCMArray(ODBus::ReadMessageIter &&i);

	void OnGetPCMsReply(ODBus::Message reply) noexcept;
	void OnOpenReply(ODBus::Message reply) noexcept;

	void SetVolume(uint16_t encoded_volume);
	void VolumeUpdate(const char *name, ODBus::ReadMessageIter &&iter);

	static DBusHandlerResult VolumeFilter(DBusConnection *conn,
                                   DBusMessage *message, void *data) noexcept;

	void OnChangeVolumeReply(ODBus::Message reply) noexcept;

};

} // namespace Bluealsa

#endif
