/*

Copyright (c) 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <cstdio> // for snprintf
#include <cinttypes> // for PRId64 et.al.
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string_view>
#include <variant>
#include <time.h>

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/span.hpp"
#include "common.hpp"

#if defined _WIN32
#include <io.h> // for _isatty
#define isatty(x) _isatty(x)
#define fileno(x) _fileno(x)
#else
#include <unistd.h> // for isatty
#endif

using namespace std::string_view_literals;

namespace {

void print_usage()
{
	std::cout << R"(usage: torrent-print [OPTIONS] torrent-files...

-h, --help               Show this message

PRINT OPTIONS:
-f, --files              List files in torrent(s)
-n, --piece-count        Print number of pieces
--piece-size             Print the piece size
--info-hash              Print the info-hash(es), both v1 and v2
--comment                Print the comment field
--creator                Print the creator field
--date                   Print the creation date field
--name                   Print the torrent name
--private                Print the private field
--trackers               Print trackers
--web-seeds              Print web-seeds
--dht-nodes              Print DHT-nodes
)"

#if LIBTORRENT_VERSION_NUM >= 30000
R"(--total-size             Print the sum of all (non-pad) files
)"
#endif
R"(FILE PRINT OPTIONS:
--file-roots             Print file merkle root hashes
--no-file-attributes     Don't print file attributes
--file-offsets           Print file offsets
--file-piece-range       Print first and last piece index for files
--no-file-size           Don't print file sizes
--file-mtime             Print file modification time (if available)
--tree                   Print file structure as a tree (default)
--flat                   Print file structure as a flat list
--no-color               Disable color escape sequences in output
--color                  Force printing colors in output
-H, --human-readable     Print file sizes with SI prefixed units

PARSE OPTIONS:
--items-limit <count>    Set the upper limit of the number of bencode items
                         in the torrent file.
--depth-limit <count>    Set the recursion limit in the bdecoder
--show-padfiles          Show pad files in file list
--max-pieces <count>     Set the upper limit on the number of pieces to
                         load in the torrent.
--max-size <size>        Reject files larger than this size limit, specified in MB

By default, all properties of torrents are printed. If any option is specified
to print a specific property, only those specified are printed.

Colored output is enabled by default, as long as stdout is a TTY. Forcing color
output on and off can be done with the --no-color and --color options.
)";
}

bool show_pad = false;
bool print_file_roots = false;
bool print_file_attributes = true;
bool print_file_offsets = false;
bool print_file_piece_range = false;
bool print_file_size = true;
bool print_file_mtime = false;
bool print_tree = true;
bool print_colors = true;
bool print_human_readable = false;

std::string human_readable(std::int64_t val)
{
	std::stringstream ret;

	ret << std::fixed;
	if (val > std::int64_t(1024) * 1024 * 1024 * 1024)
		ret << std::setprecision(2) << double(val) / (std::int64_t(1024) * 1024 * 1024 * 1024) << " TiB";
	else if (val > 1024 * 1024 * 1024)
		ret << std::setprecision(2) << double(val) / (1024 * 1024 * 1024) << " GiB";
	else if (val > 1024 * 1024)
		ret << std::setprecision(2) << double(val) / (1024 * 1024) << " MiB";
	else if (val > 1024)
		ret << std::setprecision(2) << double(val) / 1024 << " kiB";
	else
		ret << val;
	return ret.str();
}

std::string print_timestamp(std::time_t const t)
{
	if (t == 0) return "-";
	tm* fields = ::gmtime(&t);
	std::stringstream str;
	str << (fields->tm_year + 1900) << "-"
		<< std::setw(2) << std::setfill('0') << (fields->tm_mon + 1) << "-"
		<< std::setw(2) << std::setfill('0') << fields->tm_mday << " "
		<< std::setw(2) << std::setfill('0') << fields->tm_hour << ":"
		<< std::setw(2) << std::setfill('0') << fields->tm_min << ":"
		<< std::setw(2) << std::setfill('0') << fields->tm_sec;
	return str.str();
}

void print_file_attrs(lt::file_storage const& st, lt::file_index_t i)
{
	if (print_file_offsets) {
		std::cout << std::setw(11) << st.file_offset(i) << " ";
	}

	if (print_file_size) {
		std::cout << std::setw(11);
		if (print_human_readable)
			std::cout << human_readable(st.file_size(i));
		else
			std::cout << st.file_size(i);
	}

	if (print_file_attributes) {
		auto const flags = st.file_flags(i);
		std::cout << " "
			<< ((flags & lt::file_storage::flag_pad_file)?'p':'-')
			<< ((flags & lt::file_storage::flag_executable)?'x':'-')
			<< ((flags & lt::file_storage::flag_hidden)?'h':'-')
			<< ((flags & lt::file_storage::flag_symlink)?'l':'-')
			<< " ";
	}

	if (print_file_piece_range) {
		auto const first = st.map_file(i, 0, 0).piece;
		auto const last = st.map_file(i, std::max(std::int64_t(st.file_size(i)) - 1, std::int64_t(0)), 0).piece;
		std::cout << " [ "
			<< std::setw(5) << static_cast<int>(first) << ", "
			<< std::setw(5) << static_cast<int>(last) << " ] ";
	}

	if (print_file_mtime) {
		if (st.mtime(i) == 0) {
			std::cout << "                    ";
		}
		else {
			std::cout << print_timestamp(st.mtime(i)) << " ";
		}
	}

	if (print_file_roots && !st.root(i).is_all_zeros())
		std::cout << st.root(i) << " ";
}

void print_blank_attrs(bool const v2)
{
	if (print_file_offsets) {
		std::cout << "            ";
	}

	if (print_file_size) {
		std::cout << "           ";
	}

	if (print_file_attributes) {
		std::cout << "      ";
	}

	if (print_file_piece_range) {
		std::cout << "                  ";
	}

	if (print_file_mtime) {
		std::cout << "                    ";
	}

	if (print_file_roots && v2)
		std::cout << "                                                                 ";
}

bool pick_color(lt::file_flags_t const flags, bool const directory = false)
{
	if (!print_colors) return false;

	if (flags & lt::file_storage::flag_symlink) {
		std::cout << "\x1b[35m";
		return true;
	}

	if (directory) {
		std::cout << "\x1b[34m";
		return true;
	}

	if (flags & lt::file_storage::flag_executable) {
		std::cout << "\x1b[31m";
		return true;
	}

	if (flags & lt::file_storage::flag_hidden) {
		std::cout << "\x1b[36m";
		return true;
	}

	if (flags & lt::file_storage::flag_pad_file) {
		std::cout << "\x1b[33m";
		return true;
	}

	return false;
}

void print_file_list(lt::file_storage const& st)
{
	for (auto const i : st.file_range())
	{
		auto const flags = st.file_flags(i);
		if ((flags & lt::file_storage::flag_pad_file) && !show_pad) continue;

		print_file_attrs(st, i);

		bool const terminate_color = pick_color(flags);
		std::cout << st.file_path(i);
		if (terminate_color) std::cout << "\x1b[39m";

		if (flags & lt::file_storage::flag_symlink) {
			std::cout << " -> " << st.symlink(i);
		}
		std::cout << '\n';
	}
}

struct directory_entry;

using directory_entry_t
	= std::variant<
		std::map<std::string, directory_entry>,
		lt::file_index_t>;

struct directory_entry {
	directory_entry_t e;
};

void parse_single_file(std::map<std::string, directory_entry>& dir
	, std::string path, lt::file_index_t idx)
{
	auto const [left, right] = left_split(path);
	if (right.empty()) {
		// this is just the filename
		dir.insert({left, directory_entry{idx}});
	}
	else {
		// this has a parent path. add it first
		directory_entry_t& d = dir[left].e;
		if (d.index() != 0) {
			throw std::runtime_error("file clash with directory");
		}
		parse_single_file(std::get<0>(d), right, idx);
	}
}

directory_entry parse_file_list(lt::file_storage const& st)
{
	directory_entry tree;
	for (auto const i : st.file_range())
	{
		auto const flags = st.file_flags(i);
		if ((flags & lt::file_storage::flag_pad_file) && !show_pad) continue;
		parse_single_file(std::get<0>(tree.e), st.file_path(i), i);
	}
	return tree;
}

void print_tree_impl(lt::file_storage const& st, std::vector<bool>& levels
	, std::map<std::string, directory_entry> const& tree)
{
	std::size_t counter = 0;

	for (auto const& [name, e] : tree) {

		if (e.e.index() == 1) {
			print_file_attrs(st, std::get<1>(e.e));
		}
		else {
			// print the indentation
			print_blank_attrs(st.v2());
		}

		++counter;
		bool const last = counter == tree.size();
		for (bool l : levels) {
			if (l)
				std::cout << " \u2502";
			else
				std::cout << "  ";
		}

		if (last) {
			std::cout << " \u2514 ";
		}
		else {
			std::cout << " \u251c ";
		}

		if (e.e.index() == 1) {
			auto const i = std::get<1>(e.e);
			auto const flags = st.file_flags(i);

			bool const terminate_color = pick_color(flags);
			std::cout << name;
			if (terminate_color) std::cout << "\x1b[39m";

			if (flags & lt::file_storage::flag_symlink) {
				std::cout << " -> " << st.symlink(i);
			}
		}
		else {
			bool const terminate_color = pick_color({}, true);
			std::cout << name;
			if (terminate_color) std::cout << "\x1b[39m";
		}
		std::cout << '\n';

		if (e.e.index() == 0) {
			// this is a directory, add another level
			levels.push_back(!last);
			print_tree_impl(st, levels, std::get<0>(e.e));
			levels.resize(levels.size() - 1);
		}
	}
}

void print_file_tree(lt::file_storage const& st)
{
	std::vector<bool> levels;
	print_tree_impl(st, levels, std::get<0>(parse_file_list(st).e));
}
}

int main(int argc, char const* argv[]) try
{
	lt::span<char const*> args(argv, argc);
	// strip executable name
	args = args.subspan(1);

	lt::load_torrent_limits cfg;
	bool print_files = false;
	bool print_piece_count = false;
	bool print_piece_size = false;
	bool print_info_hash = false;
	bool print_comment = false;
	bool print_creator = false;
	bool print_date = false;
	bool print_name = false;
	bool print_private = false;
	bool print_trackers = false;
	bool print_web_seeds = false;
	bool print_dht_nodes = false;
#if LIBTORRENT_VERSION_NUM >= 30000
	bool print_size_on_disk = false;
#endif

	bool print_all = true;

	if (!isatty(fileno(stdout))) {
		print_colors = false;
	}

	if (args.empty()) {
		print_usage();
		return 1;
	}

	using namespace lt::literals;

	while (!args.empty() && args[0][0] == '-') {

		if (args[0] == "-f"sv || args[0] == "--files"sv)
		{
			print_files = true;
			print_all = false;
		}
		else if (args[0] == "-n"sv || args[0] == "--piece-count"sv)
		{
			print_piece_count = true;
			print_all = false;
		}
		else if (args[0] == "--piece-size"sv)
		{
			print_piece_size = true;
			print_all = false;
		}
		else if (args[0] == "--info-hash"sv)
		{
			print_info_hash = true;
			print_all = false;
		}
		else if (args[0] == "--comment"sv)
		{
			print_comment = true;
			print_all = false;
		}
		else if (args[0] == "--creator"sv)
		{
			print_creator = true;
			print_all = false;
		}
		else if (args[0] == "--date"sv)
		{
			print_date = true;
			print_all = false;
		}
		else if (args[0] == "--name"sv)
		{
			print_name = true;
			print_all = false;
		}
		else if (args[0] == "--private"sv)
		{
			print_private = true;
			print_all = false;
		}
		else if (args[0] == "--trackers"sv)
		{
			print_trackers = true;
			print_all = false;
		}
		else if (args[0] == "--web-seeds"sv)
		{
			print_web_seeds = true;
			print_all = false;
		}
		else if (args[0] == "--dht-nodes"sv)
		{
			print_dht_nodes = true;
			print_all = false;
		}
#if LIBTORRENT_VERSION_NUM >= 30000
		else if (args[0] == "--total-size"sv)
		{
			print_size_on_disk = true;
			print_all = false;
		}
#endif
		else if (args[0] == "-H"sv || args[0] == "--human-readable"sv)
		{
			print_human_readable = true;
		}
		else if (args[0] == "--tree"sv)
		{
			print_tree = true;
		}
		else if (args[0] == "--flat"sv)
		{
			print_tree = false;
		}
		else if (args[0] == "--colors"sv)
		{
			print_colors = true;
		}
		else if (args[0] == "--no-colors"sv)
		{
			print_colors = false;
		}
		else if (args[0] == "--file-roots"sv)
		{
			print_file_roots = true;
		}
		else if (args[0] == "--no-file-attributes"sv)
		{
			print_file_attributes = false;
		}
		else if (args[0] == "--file-offsets"sv)
		{
			print_file_offsets = true;
		}
		else if (args[0] == "--file-piece-range"sv)
		{
			print_file_piece_range = true;
		}
		else if (args[0] == "--no-file-size"sv)
		{
			print_file_size = false;
		}
		else if (args[0] == "--file-mtime"sv)
		{
			print_file_mtime = true;
		}
		else if (args[0] == "--items-limit"sv && args.size() > 1)
		{
			cfg.max_decode_tokens = atoi(args[1]);
			args = args.subspan(1);
		}
		else if (args[0] == "--depth-limit"sv && args.size() > 1)
		{
			cfg.max_decode_depth = atoi(args[1]);
			args = args.subspan(1);
		}
		else if (args[0] == "--max-pieces"sv && args.size() > 1)
		{
			cfg.max_pieces = atoi(args[1]);
			args = args.subspan(1);
		}
		else if (args[0] == "--max-size"sv && args.size() > 1)
		{
			cfg.max_buffer_size = atoi(args[1]) * 1024 * 1024;
			args = args.subspan(1);
		}
		else if (args[0] == "--show-padfiles"sv)
		{
			show_pad = true;
		}
		else if (args[0] == "-h"sv || args[0] == "--help"sv) {
			print_usage();
			return 0;
		}
		else
		{
			std::cerr << "unknown option " << args[0] << '\n';
			print_usage();
			return 1;
		}
		args = args.subspan(1);
	}

	for (auto const filename : args) {

		lt::torrent_info const t(filename, cfg);

		if (args.size() > 1) {
			std::cout << filename << ":\n";
		}

		// print info about torrent
		if ((print_all && !t.nodes().empty()) || print_dht_nodes)
		{
			std::cout << "nodes:\n";
			for (auto const& i : t.nodes())
				std::cout << i.first << ": " << i.second << "\n";
		}

#if LIBTORRENT_VERSION_NUM >= 30000
		if (print_all || print_size_on_disk)
		{
			std::cout << "size: " << t.size_on_disk() << "\n";
		}
#endif

		if ((print_all && !t.trackers().empty()) || print_trackers)
		{
			std::cout << "trackers:\n";
			for (auto const& i : t.trackers())
				std::cout << std::setw(2) << int(i.tier) << ": " << i.url << "\n";
		}

		if ((print_all && !t.web_seeds().empty()) || print_web_seeds) {
			std::cout << "web seeds:\n";
			for (auto const& ws : t.web_seeds())
			{
				std::cout << (ws.type == lt::web_seed_entry::url_seed ? "BEP19" : "BEP17")
					<< " " << ws.url << "\n";
			}
		}

		if (print_all || print_piece_count) {
			std::cout << "piece-count: " << t.num_pieces() << '\n';
		}

		if (print_all || print_piece_size ) {
			std::cout << "piece size: " << t.piece_length() << '\n';
		}
		if (print_all || print_info_hash) {
			std::cout << "info hash:";
			if (t.info_hashes().has_v1())
				std::cout << " v1: " << t.info_hashes().v1;
			if (t.info_hashes().has_v2())
				std::cout << " v2: " << t.info_hashes().v2;
			std::cout << '\n';
		}

		if ((print_all && !t.comment().empty()) || print_comment) {
			std::cout << "comment: " << t.comment() << '\n';
		}
		if ((print_all && !t.creator().empty()) || print_creator) {
			std::cout << "created by: " << t.creator() << '\n';
		}
		if ((print_all && t.creation_date() != 0) || print_date) {
			std::cout << "creation date: " << print_timestamp(t.creation_date()) << '\n';
		}
		if ((print_all && t.priv()) || print_private) {
			std::cout << "private: " << (t.priv() ? "yes" : "no") << "\n";
		}
		if (print_all || print_name) {
			std::cout << "name: " << t.name() << '\n';
		}
		if (print_all) {
			std::cout << "number of files: " << t.num_files() << '\n';
		}

		if (print_all || print_files) {
			std::cout << "files:\n";
			lt::file_storage const& st = t.files();
			if (print_tree) {
				print_file_tree(st);
			}
			else {
				print_file_list(st);
			}
		}
	}
}
catch (std::exception const& e)
{
	std::cerr << "failed: " << e.what() << '\n';
}
