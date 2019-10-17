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

#ifndef _MYTHTV_CLIENT_HXX
#define _MYTHTV_CLIENT_HXX

#include "tag/Tag.hxx"
#include "song/DetachedSong.hxx"

#include <string>
#include <vector>
#include <forward_list>

#include <time.h>
#include <math.h>

#ifdef PROTOCOL_VERSION
#undef PROTOCOL_VERSION
#endif
#include <mysql++/mysql++.h>


class MythtvClient {
	const char *host;
	const char *database;
	const char *user;
	const char *password;
	const char *prefix;
	mysqlpp::Connection conn;

public:

	MythtvClient();

	MythtvClient(const char *hostname, const char *dbname,
	             const char *username, const char *pwd,
	             const char *url);

	~MythtvClient();

	void Open();

	void Close();

	void SetHostname(const char *hostname);
	void SetDBName(const char *dbname);
	void SetUserName(const char *username);
	void SetPassword(const char *pwd);
	void SetRecordingsUrl(const char *url);
	void SetConfig(const char *hostname, const char *dbname,
	               const char *username, const char *pwd,
                   const char *location);

	mysqlpp::StoreQueryResult QueryMythtvDatabase(const std::string &query);

	static int CalculateDuration(const char *start, const char *end);

	static std::unique_ptr<Tag> CreateTag(const mysqlpp::Row &row);

	std::unique_ptr<Tag> GetMetaData(const char *filename);

	std::forward_list<DetachedSong>
	GetRecordings(std::vector<std::string> &filter);

	const char *RecordingsUrl() const;
};

#endif
