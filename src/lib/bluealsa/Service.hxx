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

#ifndef BLUEALSA_SERVICE_HXX
#define BLUEALSA_SERVICE_HXX

#include "lib/dbus/Glue.hxx"
#include "thread/SafeSingleton.hxx"
#include "util/Manual.hxx"

#include <string>

class EventLoop;

namespace Bluealsa {

/**
 * Encapsulates the DBus connection and service parameters of the Bluealsa
 * DBus API.
 */
class Service {

	static constexpr auto path_prefix = "/org/bluealsa";
	static constexpr auto default_name = "org.bluealsa";

	std::string name;

	EventLoop &eventloop;
	Manual<SafeSingleton<ODBus::Glue>> dbus;
	bool running = false;

public:

	Service(EventLoop &event_loop, const char *suffix = nullptr) noexcept
		: name(default_name)
		, eventloop(event_loop)
	{
		if (suffix && *suffix)
			name = name + "." + suffix;
	}

	~Service() {
		Stop();
	}

	void Start() {
		if (!running) {
			dbus.Construct(eventloop);
			running = true;
		}
	}

	void Stop() {
		if (running) {
			running = false;
			dbus.Destruct();
		}
	}

	const char *Name() const noexcept {
		return name.c_str();
	}

	const char *Path() const noexcept {
		return path_prefix;
	}

	ODBus::Connection &Connection() noexcept {
		return dbus.Get()->GetConnection();
	}

	/**
	 * dbus may have already have been configured to use an event loop
	 * different to the one that was passed to our constructor.
	 * So we use the loop reported by dbus.
	 */
	EventLoop &GetEventLoop() noexcept {
		return dbus.Get()->GetEventLoop();
	}
};

} // namespace Bluealsa

#endif
