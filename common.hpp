/*

Copyright (c) 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#pragma once

#include "libtorrent/version.hpp"

#include <functional> // for std::hash
#include <string>
#include <vector>
#include <fstream>

#if LIBTORRENT_VERSION_NUM <= 20002

namespace std {

	template<>
	struct hash<lt::sha256_hash>
	{
		using argument_type = lt::sha256_hash;
		using result_type = std::size_t;
		result_type operator()(argument_type const& s) const
		{
			result_type ret;
			std::memcpy(&ret, s.data(), sizeof(ret));
			return ret;
		}
	};
}

#endif

inline std::vector<char> load_file(std::string const& filename)
{
	std::fstream in;
	in.exceptions(std::ifstream::failbit);
	in.open(filename.c_str(), std::ios_base::in | std::ios_base::binary);
	in.seekg(0, std::ios_base::end);
	size_t const size = size_t(in.tellg());
	in.seekg(0, std::ios_base::beg);
	std::vector<char> ret(size);
	in.read(ret.data(), int(ret.size()));
	return ret;
}

inline std::string branch_path(std::string const& f)
{
	if (f.empty()) return f;

#ifdef TORRENT_WINDOWS
	if (f == "\\\\") return "";
#endif
	if (f == "/") return "";

	auto len = f.size();
	// if the last character is / or \ ignore it
	if (f[len-1] == '/' || f[len-1] == '\\') --len;
	while (len > 0) {
		--len;
		if (f[len] == '/' || f[len] == '\\')
			break;
	}

	if (f[len] == '/' || f[len] == '\\') ++len;
	return std::string(f.c_str(), len);
}

inline std::pair<std::string, std::string> left_split(std::string const& f)
{
	if (f.empty()) return {};

	for (std::size_t i = 0; i < f.size(); ++i) {
		if (f[i] == '/' || f[i] == '\\')
			return {f.substr(0, i), f.substr(i + 1)};
	}
	return {f, {}};
}

inline std::pair<std::string, std::string> right_split(std::string const& f)
{
	if (f.empty()) return {};

	for (std::size_t i = f.size(); i > 0; --i) {
		if (f[i - 1] == '/' || f[i - 1] == '\\')
			return {f.substr(0, i - 1), f.substr(i)};
	}
	return {f, {}};
}


inline std::string replace_directory_element(std::string const& path, std::string const& name)
{
	auto const [dir, rest] = left_split(path);
#ifdef TORRENT_WINDOWS
	return name + '\\' + rest;
#else
	return name + '/' + rest;
#endif
}

