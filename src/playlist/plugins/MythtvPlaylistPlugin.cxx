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

#include "MythtvPlaylistPlugin.hxx"
#include "../PlaylistPlugin.hxx"
#include "../SongEnumerator.hxx"
#include "../MemorySongEnumerator.hxx"
#include "PlaylistError.hxx"
#include "util/SplitString.hxx"
#include "thread/Mutex.hxx"
#include "lib/mythtv/MythtvInstance.hxx"

#include <string>
#include <forward_list>
#include <vector>



static std::unique_ptr<SongEnumerator>
mythtv_open_uri(const char *uri, Mutex &mutex)
{
	assert(memcmp(uri, "mythtv://", 9) == 0);
	uri += 9;

	std::vector<std::string> filter;
	auto args = SplitString(uri, '/', false);
	if (!args.empty()) {
		if (args.front() == "radio") {
			filter.push_back("channel.channum >= 700 and channel.channum < 800");
			args.pop_front();
		} else if (args.front() == "tv") {
			filter.push_back("(channel.channum < 700 or channel.channum >= 800)");
			args.pop_front();
		} else if (args.front() == "all") {
			args.pop_front();
		}
	}
	std::lock_guard<Mutex> protect(mutex);
	return std::make_unique<MemorySongEnumerator>(std::move(::mythtv.GetRecordings(filter)));
}



static const char *const mythtv_schemes[] = {
	"mythtv",
	nullptr
};

const PlaylistPlugin mythtv_playlist_plugin =
	PlaylistPlugin("mythtv", mythtv_open_uri)
	.WithSchemes(mythtv_schemes);

