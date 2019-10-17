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
#include "tag/Tag.hxx"
#include "tag/Builder.hxx"
#include "MythtvClient.hxx"

#include <string>
#include <stdexcept>

#include <time.h>
#include <math.h>

#ifdef PROTOCOL_VERSION
#undef PROTOCOL_VERSION
#endif
#include <mysql++/mysql++.h>


MythtvClient::MythtvClient() :
	host("localhost"),
	database("mythconverg"),
	user("mythtv"),
	password(""),
	prefix("/var/lib/mythtv/recordings"),
	conn(false)
	{}

MythtvClient::MythtvClient(const char *hostname, const char *dbname,
	                       const char *username, const char *pwd,
                           const char *location) :
	host(hostname),
	database(dbname),
	user(username),
	password(pwd),
	prefix(location),
	conn(false)
	{}

MythtvClient::~MythtvClient() {
	Close();
}

void MythtvClient::Open() {
	if (!conn.connected()) {
		conn.set_option(new mysqlpp::ReconnectOption(true));
		if (!conn.connect(database, host, user, password))
			throw std::runtime_error("cannot connect to mythtv database");
	}
}

void MythtvClient::Close() {
		conn.disconnect();
}

void MythtvClient::SetHostname(const char *hostname) {
	host = hostname;
}

void MythtvClient::SetDBName(const char *dbname) {
	database = dbname;
}


void MythtvClient::SetUserName(const char *username) {
	user = username;
}


void MythtvClient::SetPassword(const char *pwd) {
	password = pwd;
}


void MythtvClient::SetRecordingsUrl(const char *url) {
	prefix = url;
}

void MythtvClient::SetConfig(const char *hostname, const char *dbname,
	                       const char *username, const char *pwd,
                           const char *location) {
	host = hostname;
	database = dbname;
	user = username;
	password = pwd;
	prefix = location;
	if (conn.connected()) {
		Close();
		Open();
	}
}

int MythtvClient::CalculateDuration(const char *start, const char *end) {
	constexpr const char *dateformat = "%Y-%m-%d %H:%M:%S";
	struct tm start_tm, end_tm;
	if (strptime(start, dateformat, &start_tm) == nullptr)
		return -1;
	if (strptime(end, dateformat, &end_tm) == nullptr)
		return -1;
	return round(difftime(mktime(&end_tm), mktime(&start_tm)));
}

std::unique_ptr<Tag> MythtvClient::CreateTag(const mysqlpp::Row &row) {
	TagBuilder tag;
	tag.AddItem(TAG_TITLE, row["title"].c_str());
	tag.AddItem(TAG_COMMENT, row["subtitle"].c_str());
	tag.AddItem(TAG_NAME, row["channel"].c_str());
	tag.AddItem(TAG_DATE, row["starttime"].c_str());

	int duration = CalculateDuration(row["starttime"],
	                                 row["endtime"]);
	tag.SetDuration(SignedSongTime::FromS(duration));

	return tag.CommitNew();
}

std::unique_ptr<Tag> MythtvClient::GetMetaData(const char *filename) {
	assert(filename != nullptr);

	if (!conn.connected())
		throw std::runtime_error("Not connected to Mythtv");

	mysqlpp::Query q = conn.query();
	q << "SELECT title, subtitle, channel.name AS channel, starttime, "
	     "endtime FROM recorded inner join channel on "
	     "recorded.chanid = channel.chanid WHERE basename = '";
	q << filename;
	q << "'";

	auto res = q.store();
	if (res.num_rows() < 1)
		throw std::runtime_error("file not found");

	return CreateTag(res[0]);
}

std::forward_list<DetachedSong>
MythtvClient::GetRecordings(std::vector<std::string> &filter) {
	std::forward_list<DetachedSong> songs;

	if (!conn.connected())
		return songs;

	mysqlpp::Query q = conn.query();
	q << "SELECT title, subtitle, channel.name AS channel, starttime, "
         "endtime, basename FROM recorded inner join channel "
         "on recorded.chanid = channel.chanid WHERE recgroup <> 'Deleted'";

	for (auto f : filter) {
		q << " AND " << f;
	}

	q << " ORDER BY channel, title, starttime";

	auto res = q.store();

	auto s = songs.before_begin();
	for (size_t i = 0; i < res.num_rows(); ++i) {
		std::string uri("mythtv://");
		uri += res[i]["basename"].c_str();
		auto tag = CreateTag(res[i]);
		s = songs.emplace_after(s, uri.c_str(), std::move(*tag));
	}

	return songs;
}

const char *MythtvClient::RecordingsUrl() const {
	return prefix;
}
