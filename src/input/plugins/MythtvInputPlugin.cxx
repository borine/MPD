/*
 * Copyright 2003-2016 The Music Player Daemon Project
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

#include "config.h"
#include "MythtvInputPlugin.hxx"
#include "../InputPlugin.hxx"
#include "../ProxyInputStream.hxx"
#include "config/Block.hxx"
#include "PluginUnavailable.hxx"
#include "util/StringAPI.hxx"
#include "util/StringCompare.hxx"
#include "lib/mythtv/MythtvInstance.hxx"
#include "thread/Mutex.hxx"

#include <stdio.h>

static constexpr size_t maxPrefix = 1000;
static constexpr size_t maxName = 24;



class MythtvInputStream final : public ProxyInputStream {
public:
	MythtvInputStream(const char *url, Mutex &_mutex)
		:ProxyInputStream(url, _mutex) {

		const char *filename = url + 9;
		char *newurl = new char[strlen(::mythtv.RecordingsUrl()) + strlen(filename) + 2];
		char *p = UnsafeCopyStringP(newurl, ::mythtv.RecordingsUrl());
		if (!StringEndsWith(newurl, "/"))
			p = UnsafeCopyStringP(p, "/");
		UnsafeCopyString(p, filename);

		InputStreamPtr fis = OpenReady(newurl, mutex); // may throw runtime error

		SetMimeType("video/mp2t");
		seekable = true;

		const std::lock_guard<Mutex> protect(mutex);
		SetInput(std::move(fis));
	}

};



static void mythtv_input_init(EventLoop&, const ConfigBlock &block) {
	::mythtv.SetConfig(block.GetBlockValue("host", "localhost"),
	                   block.GetBlockValue("database", "mythconverg"),
	                   block.GetBlockValue("user", "mythtv"),
	                   block.GetBlockValue("password", "mythtv"),
	                   block.GetBlockValue("recordings_url", "/var/lib/mythtv/recordings"));
	try {
		::mythtv.Open();
	}
	catch (...) {
		throw PluginUnavailable("Cannot connect to mythtv database");
	}
}

static InputStreamPtr mythtv_input_open(const char *url, Mutex &mutex) {

	if (!StringStartsWith(url, "mythtv://"))
		return nullptr;

	if (StringIsEmpty(url + 9) || StringLength(url + 9) > maxName)
		return nullptr;

	return std::make_unique<MythtvInputStream>(url, mutex);
}

static void mythtv_input_finish()
{
	::mythtv.Close();
}

static constexpr const char *mythtv_prefixes[] = {
	"mythtv://",
	nullptr
};

#if 0
static std::unique_ptr<RemoteTagScanner> mythtv_scan_tags(const char *uri,
						                           RemoteTagHandler &handler)
{

}

#endif

const struct InputPlugin input_plugin_mythtv = {
	"mythtv",
	mythtv_prefixes,
	mythtv_input_init,
	mythtv_input_finish,
	mythtv_input_open,
	nullptr,
};
