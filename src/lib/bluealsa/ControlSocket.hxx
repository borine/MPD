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

#ifndef BLUEALSA_CONTROL_SOCKET_HXX
#define BLUEALSA_CONTROL_SOCKET_HXX

#include "net/SocketDescriptor.hxx"
#include "util/RuntimeError.hxx"
#include "util/StringCompare.hxx"

#include <stdexcept>

namespace Bluealsa {

/**
 * A socket used by Bluealsa to communicate flow control commands.
 * The protocol requires that the client waits for a response after each
 * command before sending the next.
 */
class ControlSocket : SocketDescriptor {

public:

	ControlSocket() :
		SocketDescriptor(SocketDescriptor::Undefined())
	{}

	void SendDrain(bool blocking = false) {
		SendCommand("Drain", blocking);
	}

	void SendDrop(bool blocking = false) {
		SendCommand("Drop", blocking);
	}

	void SendPause(bool blocking = false) {
		SendCommand("Pause", blocking);
	}

	void SendResume(bool blocking = false) {
		SendCommand("Resume", blocking);
	}

	void ReadReply() {
		char reply[32];
		ssize_t count = Read(reply, 31);
		if (!StringIsEqual(reply, "OK", 2)) {
			reply[count] = 0;
			throw FormatRuntimeError("Bluealsa control error: %s",
			                                                reply);
		}
	}

	using SocketDescriptor::IsDefined;
	using SocketDescriptor::IsSocket;
	using SocketDescriptor::IsValid;
	using SocketDescriptor::Get;
	using SocketDescriptor::Set;
	using SocketDescriptor::SetNonBlocking;
	using SocketDescriptor::Close;
	using SocketDescriptor::GetError;
	using SocketDescriptor::WaitReadable;
	using SocketDescriptor::WaitWritable;
	using SocketDescriptor::IsReadyForWriting;

private:

	void SendCommand(const char *command, bool blocking) {
		size_t len = StringLength(command);
		ssize_t ret = Write(command, len);
		if (ret < 0 || (size_t)ret != len)
			throw std::runtime_error("failed send command");
		if (blocking) {
			if (WaitReadable(100) != 1)
				throw std::runtime_error("Bluealsa command timed out");
			ReadReply();
		}
	}

};


} // namespace Bluealsa

#endif
