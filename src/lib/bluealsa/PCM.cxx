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

#include "Format.hxx"
#include "PCM.hxx"
#include "Service.hxx"

#include "lib/dbus/AsyncRequest.hxx"
#include "lib/dbus/Message.hxx"
#include "lib/dbus/AppendIter.hxx"
#include "lib/dbus/ReadIter.hxx"
#include "lib/dbus/ScopeMatch.hxx"
#include "system/FileDescriptor.hxx"
#include "thread/Mutex.hxx"
#include "thread/Cond.hxx"
#include "util/RuntimeError.hxx"
#include "util/StringCompare.hxx"

#include <dbus/dbus.h>

#include <algorithm>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>

namespace Bluealsa {

PCM::~PCM() noexcept
{
	CloseTransport();
	CloseMixer();
	Stop();
}

void
PCM::Configure()
{

	assert(!dbus_open_request);

	supported_modes = MODE_UNKNOWN;

	auto msg = ODBus::Message::NewMethodCall(service.Name(),
			                          service.Path(),
			                          MANAGER_INTERFACE, "GetPCMs");
	dbus_open_request.Send(service.Connection(), *msg.Get(),
			  std::bind(&PCM::OnGetPCMsReply,
					this, std::placeholders::_1));
}

void
PCM::OnGetPCMsReply(ODBus::Message reply) noexcept
{
	std::exception_ptr error;
	try {

		reply.CheckThrowError();

		ODBus::ReadMessageIter i(*reply.Get());
		if (i.GetArgType() != DBUS_TYPE_ARRAY)
			throw std::runtime_error("Malformed D-Bus response");

		ParsePCMArray(i.Recurse());
		if (!IsValid())
			throw std::runtime_error("Device not connected");

	} catch(...) {
		error = std::current_exception();
	}

	OnConfigurationComplete(error);
}

void
PCM::OpenTransport()
{
	assert (!dbus_open_request);

	if (! (supported_modes & selected_mode))
		throw std::runtime_error("Requested mode not available");

	auto msg = ODBus::Message::NewMethodCall(service.Name(),
	                                            object_path.c_str(),
	                                            PCM_INTERFACE, "Open");

	const char *modestr = selected_mode == MODE_SOURCE ? "source" : "sink";
	ODBus::AppendMessageIter(*msg.Get()).Append(modestr);

	dbus_open_request.Send(service.Connection(), *msg.Get(),
	             std::bind(&PCM::OnOpenReply,
			         this, std::placeholders::_1));
}

void
PCM::OnOpenReply(ODBus::Message reply) noexcept
{
	std::exception_ptr error;
	try {
		using namespace ODBus;

		reply.CheckThrowError();

		DBusError dbus_error = DBUS_ERROR_INIT;
		int sfd, cfd;
		if (!reply.GetArgs(dbus_error, DBUS_TYPE_UNIX_FD, &sfd,
			DBUS_TYPE_UNIX_FD, &cfd))
			throw std::runtime_error(dbus_error.message);

		stream_fd.Set(sfd);
		control_fd.Set(cfd);

		if (stream_fd.IsDefined() != control_fd.IsDefined())
			/* inconsistent and unusable state */
			throw std::runtime_error("Bluealsa service connection corrupted");

		assert(stream_fd.IsPipe() && control_fd.IsSocket());

	} catch (...) {
		error = std::current_exception();
	}

	OnOpenComplete(error);
}

void
PCM::CloseTransport() noexcept
{
	if (stream_fd.IsDefined())
		stream_fd.Close();

	if (control_fd.IsDefined())
		control_fd.Close();

	if (dbus_open_request)
		dbus_open_request.Cancel();
}

void
PCM::OpenMixer()
{
	std::lock_guard<Mutex> lock(mutex);
	try {
		if (!mixer_open) {
			volume_match.Construct(service.Connection(),
				                        vol_match_spec.c_str());

			mixer_open = true;
			if (!dbus_connection_add_filter(service.Connection(),
			                        &PCM::VolumeFilter, this, NULL))
				throw FormatRuntimeError(
				                "Couldn't add D-Bus filter: %s",
				                              strerror(ENOMEM));
		}
	} catch (...) {
		if (mixer_open) {
			volume_match.Destruct();
			mixer_open = false;
		}
		throw;
	}
}

void
PCM::ChangeVolume(uint8_t vol) {
	auto msg = ODBus::Message::NewMethodCall(service.Name(),
				      object_path.c_str(),
				      DBUS_INTERFACE_PROPERTIES, "Set");

	static const char *interface = PCM_INTERFACE;
	static const char *property = "Volume";

	uint8_t leftvol = (((vol + 1) << 7) / 101) -1;
	uint16_t value = leftvol + (leftvol << 8);

	ODBus::AppendMessageIter(*msg.Get()).Append(interface);
	ODBus::AppendMessageIter(*msg.Get()).Append(property);
	ODBus::AppendMessageIter(*msg.Get()).AppendVariant(value);

	dbus_volume_request.Send(service.Connection(), *msg.Get(),
			  std::bind(&PCM::OnChangeVolumeReply,
					this, std::placeholders::_1));
}

bool
PCM::IsValid() const noexcept
{
	return !object_path.empty() &&
	       format.IsFullyDefined() &&
	       supported_modes != MODE_UNKNOWN;
}

bool
PCM::IsOpen() const noexcept
{
	return stream_fd.IsDefined() && control_fd.IsDefined();
}

bool PCM::MatchPath(const char *test_path) noexcept {
	object_path.clear();
	if (!StringEndsWith(test_path, "/a2dp"))
		return false;
	if (address.empty() ||
		            StringFind(test_path, pattern.c_str()) != nullptr)
		object_path = test_path;
	return !object_path.empty();
}

void
PCM::Reset() noexcept
{
	object_path.clear();
	format = TransportFormat::Undefined();
	supported_modes = MODE_UNKNOWN;
}

void
PCM::Populate(const char *name, ODBus::ReadMessageIter &&i)
{
	if (StringIsEqual(name, "Device")) {
		/* This is the bluez path to the device. The address component
		 * of this path must match the address component of the
		 * bluealsa a2dp path */
		if (StringFind(i.GetString(), pattern.c_str()) == nullptr)
			throw std::runtime_error("Malformed response");
	}
	else if (StringIsEqual(name, "Modes")) {
		if (i.GetArgType() != DBUS_TYPE_ARRAY)
			throw std::runtime_error("Malformed response");
		i.Recurse().ForEach(DBUS_TYPE_STRING,
		                            [this](ODBus::ReadMessageIter &j) {
			const char *modestr = j.GetString();
			if (StringIsEqual(modestr, "source"))
				supported_modes |= MODE_SOURCE;
			else if (StringIsEqual(modestr, "sink"))
				supported_modes |= MODE_SINK;
			else
				throw std::runtime_error("Malformed response");
		});
	}
	else if (StringIsEqual(name, "Format")) {
		if (i.GetArgType() != DBUS_TYPE_UINT16)
			throw std::runtime_error("Malformed response");
		uint16_t encoded_format;
		i.GetBasic(&encoded_format);
		format.DecodeSampleFormat(encoded_format);
	}
	else if (StringIsEqual(name, "Channels")) {
		if (i.GetArgType() != DBUS_TYPE_BYTE)
			throw std::runtime_error("Malformed response");
		i.GetBasic(&format.channels);
	}
	else if (StringIsEqual(name, "Sampling")) {
		if (i.GetArgType() != DBUS_TYPE_UINT32)
			throw std::runtime_error("Malformed response");
		i.GetBasic(&format.sample_rate);
	}
	else if (StringIsEqual(name, "Volume")) {
		if (i.GetArgType() != DBUS_TYPE_UINT16)
			throw std::runtime_error("Malformed response");
		uint16_t encoded_volume;
		i.GetBasic(&encoded_volume);
		SetVolume(encoded_volume);
	}
}

bool
PCM::Match(ODBus::ReadMessageIter &&i)
{
	using namespace std::placeholders;

	if (i.GetArgType() != DBUS_TYPE_OBJECT_PATH)
		return false;

	const char *path = i.GetString();
	if (!MatchPath(path))
		return false;

	i.Next();

	if (i.GetArgType() != DBUS_TYPE_ARRAY)
		return false;

	i.Recurse().ForEachProperty(std::bind(&PCM::Populate, this, _1, _2));

	return supported_modes & selected_mode;
}

static void
GetAddressFromPath(const char *path, std::string &address)
{
	/* The path must end in "/dev_XX_XX_XX_XX_XX_XX/a2dp" */
	static constexpr auto countback = strlen("XX_XX_XX_XX_XX_XX/a2dp");
	static constexpr auto addrlen = strlen("XX_XX_XX_XX_XX_XX");
	address.assign(path + strlen(path) - countback, addrlen);
	std::replace(address.begin(), address.end(), '_', ':');
}

void
PCM::ParsePCMArray(ODBus::ReadMessageIter &&i)
{
	for (; i.GetArgType() == DBUS_TYPE_DICT_ENTRY; i.Next()) {
		if (Match(i.Recurse())) {
			if (address.empty())
				GetAddressFromPath(object_path.c_str(),
				                                      address);
			break;
		}
		Reset();
	}
}

void
PCM::CloseMixer()
{
	std::lock_guard<Mutex> lock(mutex);
	if (mixer_open) {
		volume_match.Destruct();
		dbus_connection_remove_filter(service.Connection(),
			                             &PCM::VolumeFilter, this);
		mixer_open = false;
	}
}


/*
 * encoded_volume holds PCM volume and mute information for channel 1 (left)
 * and channel 2 (right).
 * Data for channel 1 is stored in the upper byte, while channel 2 is stored
 * in the lower byte.
 * The highest bit of both bytes determines whether the channel is muted.
 * MPD uses only the left channel volume.
 * Possible A2DP values: 0-127
 */
void
PCM::SetVolume(uint16_t encoded_volume)
{
	uint8_t vol = (encoded_volume >> 8) & 0x7FFF;
	bool mute = encoded_volume & 0x8000;
	std::lock_guard<Mutex> lock(mutex);
	volume = mute ? 0 : (((vol + 1) * 101) >> 7) - 1;
}

void
PCM::VolumeUpdate(const char *name, ODBus::ReadMessageIter &&iter)
{
	if (StringIsEqual(name, "Volume")) {
		if (iter.GetArgType() != DBUS_TYPE_UINT16)
			return;

		uint16_t encoded_volume;
		iter.GetBasic(&encoded_volume);
		SetVolume(encoded_volume);
	}
}

DBusHandlerResult
PCM::VolumeFilter(DBusConnection *, DBusMessage *message, void *data) noexcept
{
	if (!dbus_message_is_signal(message,DBUS_INTERFACE_PROPERTIES,
                                                          "PropertiesChanged"))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	ODBus::ReadMessageIter iter(*message);

	const char *iface = iter.GetString();
	if (!StringIsEqual(iface, PCM_INTERFACE))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	PCM *pcm = static_cast<PCM*>(data);
	iter.Next();
	if (iter.GetArgType() == DBUS_TYPE_ARRAY)
		iter.Recurse().ForEachProperty(std::bind(&PCM::VolumeUpdate,
	                                          pcm, std::placeholders::_1,
		                                  std::placeholders::_2));

	return DBUS_HANDLER_RESULT_HANDLED;
}

void
PCM::OnChangeVolumeReply(ODBus::Message reply) noexcept
{
	std::exception_ptr error;
	try {
		reply.CheckThrowError();
	} catch (...) {
		error = std::current_exception();
	}
	// what do with to any error caught here ??
}


} // namespace Bluealsa


