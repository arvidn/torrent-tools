/*

Copyright (c) 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/


#include <iostream>
#include <string_view>

#include "libtorrent/create_torrent.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bdecode.hpp"

#include "common.hpp"

using namespace std::string_view_literals;

namespace {

void print_usage()
{
	std::cout << R"(USAGE: torrent-add torrent-file [OPTIONS] files...
OPTIONS:
-o, --out <file>          Print resulting torrent to the specified file.
                          If not specified "a.torrent" is used.
-m, --mtime               Include modification time of files
-l, --dont-follow-links   Instead of following symlinks, store them as symlinks
-h, --help                Show this message
-q                        Quiet, do not print log messages

Reads torrent-file and adds the files, specified by "files...". The resulting
torrent is written to the output file specified by -o (or a.torrent by
default).

Only BitTorrent v2 torrent files are supported.
)";
}

} // anonymous namespace

int main(int argc_, char const* argv_[]) try
{
	lt::span<char const*> args(argv_, argc_);
	// strip executable name
	args = args.subspan(1);

	if (args.size() < 2) {
		print_usage();
		return 1;
	}

	std::string input_file = args[0];
	args = args.subspan(1);
	std::string output_file = "a.torrent";
	bool quiet = false;
	lt::create_flags_t flags = lt::create_torrent::v2_only;

	while (args.size() > 0 && args[0][0] == '-') {

		if ((args[0] == "-o"sv || args[0] == "--out"sv) && args.size() > 1) {
			output_file = args[1];
			args = args.subspan(1);
		}
		else if (args[0] == "-q"sv) {
			quiet = true;
		}
		else if (args[0] == "-m"sv || args[0] == "--mtime"sv) {
			flags |= lt::create_torrent::modification_time;
		}
		else if (args[0] == "-l"sv || args[0] == "--dont-follow-links"sv) {
			flags |= lt::create_torrent::symlinks;
		}
		else if (args[0] == "-h"sv || args[0] == "--help"sv) {
			print_usage();
			return 0;
		}
		else {
			std::cerr << "unknown option " << args[0] << '\n';
			print_usage();
			return 1;
		}
		args = args.subspan(1);
	}

	if (args.empty()) {
		std::cerr << "no files to add\n";
		print_usage();
		return 1;
	}

	auto input = load_file(input_file);
	auto torrent_node = lt::bdecode(input);
	lt::entry torrent_e(torrent_node);

	int const piece_size = torrent_e["info"]["piece length"].integer();

	std::cout << "piece size: " << piece_size << '\n';

	auto& p_layers = torrent_e["piece layers"].dict();
	auto& file_tree = torrent_e["info"]["file tree"].dict();

	for (auto const file : args) {

		if (!quiet) std::cout << "adding " << file << '\n';
		lt::file_storage fs;
		fs.set_piece_length(piece_size);
		lt::add_files(fs, file, [](std::string const&) { return true; }, flags);
		lt::create_torrent creator(fs, piece_size, flags);

		auto const num = creator.num_pieces();
		lt::set_piece_hashes(creator, branch_path(file)
			, [num, quiet] (lt::piece_index_t const p) {
				if (quiet) return;
				std::cout << "\r" << p << "/" << num;
				std::cout.flush();
			});
		if (!quiet) std::cout << "\n";

		auto e = creator.generate();

		auto file_entry = *e["info"]["file tree"].dict().begin();
		file_tree.insert(std::move(file_entry));

		auto& new_p_layers = e["piece layers"].dict();
		// not all files have a piece layer. Small ones for instance
		if (!new_p_layers.empty())
			p_layers.insert(std::move(*new_p_layers.begin()));
	}

	std::vector<char> torrent;
	lt::bencode(back_inserter(torrent), torrent_e);
	std::fstream out;
	out.exceptions(std::ifstream::failbit);
	out.open(output_file.c_str(), std::ios_base::out | std::ios_base::binary);

	if (!quiet) std::cout << "-> writing to " << output_file << "\n";
	out.write(torrent.data(), int(torrent.size()));
}
catch (std::exception const& e)
{
	std::cerr << "failed: " << e.what() << '\n';
}

