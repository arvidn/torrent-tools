/*

Copyright (c) 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/settings_pack.hpp"

#include "common.hpp"

#include <functional>
#include <cstdio>
#include <sstream>
#include <fstream>
#include <iostream>
#include <thread>

#ifdef TORRENT_WINDOWS
#include <direct.h> // for _getcwd
#endif

#include <string_view>

using namespace std::string_view_literals;

namespace {

using namespace std::placeholders;

// do not include files and folders whose
// name starts with a .
bool file_filter(std::string const& f)
{
	if (f.empty()) return false;

	char const* first = f.c_str();
	char const* sep = strrchr(first, '/');
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
	char const* altsep = strrchr(first, '\\');
	if (sep == nullptr || altsep > sep) sep = altsep;
#endif
	// if there is no parent path, just set 'sep'
	// to point to the filename.
	// if there is a parent path, skip the '/' character
	if (sep == nullptr) sep = first;
	else ++sep;

	// return false if the first character of the filename is a .
	if (sep[0] == '.') return false;

	std::cerr << f << "\n";
	return true;
}

int const default_num_threads
	= std::max(1, static_cast<int>(std::thread::hardware_concurrency()));

void print_usage()
{
	std::cerr << R"(USAGE: torrent-new [OPTIONS] file

Generates a torrent file from the specified file
or directory and writes it to an output .torrent file

OPTIONS:
-o, --out <file>             Print resulting torrent to the specified file.
                             If not specified "a.torrent" is used.
-t, --tracker <url>          Add <url> as a tracker in a new tier.
-T, --tracker-tier <url>     Add <url> as a tracker in the current tier.
-w, --web-seed <url>         Add <url> as a web seed to the torrent.
-d, --dht-node <host> <port> Add a DHT node to the torrent, that can be used to
                             bootstrap the DHT network from.
-C, --creator <name>         sets the "created by" field to <name>.
-c, --comment <str>          Sets the "comment" field to <str>.
-p, --private                Set the "private" field to 1.
-h, --help                   Show this message
-l, --dont-follow-links      Instead of following symlinks, store them as symlinks
                             in the .torrent file
-2, --v2-only                Generate a BitTorrent v2-only torrent (not compatible with v1)
-m, --mtime                  Include modification time of files
-s, --piece-size <size>      Specifies the piece size, in kiB. This must be at least
                             16kiB and must be a power of 2.
-r, --root-cert <file>       Embed the specified root certificate in the torrent file
                             (for SSL torrents only). All peers and trackers must
                             authenticate with a cert signed by this root, directly
                             or indirectly.

--threads <n>                Use <n> threads to hash pieces. Defaults to )"
	<< default_num_threads << R"(.

To manage tracker tiers -t will add a new tier immediately before adding the
tracker whereas -T will add the tracker to the current tier. If there is no
tier, one will be created regardless of which flavour of -t and -T is used. e.g.

  -t https://foo.com -t https://bar.com

Will add foo and bar as separate tiers.

  -t https://foo.com -T https://bar.com

Will add foo and bar as the same tier.
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

	std::string creator = "torrent-tools";
	std::string comment_str;
	bool private_torrent = false;
	std::vector<std::string> web_seeds;
	std::vector<std::pair<std::string, int>> dht_nodes;
	std::vector<std::vector<std::string>> trackers;
	int piece_size = 0;
	lt::create_flags_t flags = {};
	std::string root_cert;
	bool quiet = false;
	int num_threads = default_num_threads;

	std::string output_file = "a.torrent";

	while (args.size() > 0 && args[0][0] == '-') {

		if ((args[0] == "-o"sv || args[0] == "--out"sv) && args.size() > 1) {
			output_file = args[1];
			args = args.subspan(1);
		}
		else if (args[0] == "--threads"sv && args.size() > 1) {
			num_threads = atoi(args[1]);
			args = args.subspan(1);
		}
		else if ((args[0] == "-t"sv || args[0] == "--tracker"sv) && args.size() > 1) {
			std::string t = args[1];
			args = args.subspan(1);
			trackers.emplace_back(std::vector<std::string>{std::move(t)});
		}
		else if ((args[0] == "-T"sv || args[0] == "--tracker-tier"sv) && args.size() > 1) {
			std::string t = args[1];
			args = args.subspan(1);
			if (trackers.empty())
				trackers.emplace_back(std::vector<std::string>{std::move(t)});
			else
				trackers.back().emplace_back(std::move(t));
		}
		else if ((args[0] == "-w"sv || args[0] == "--web-seed"sv) && args.size() > 1) {
			web_seeds.emplace_back(args[1]);
			args = args.subspan(1);
		}
		else if (args[0] == "--dht-node"sv && args.size() > 2) {
			dht_nodes.emplace_back(args[1], std::atoi(args[2]));
			args = args.subspan(2);
		}
		else if ((args[0] == "-C"sv || args[0] == "--creator"sv) && args.size() > 1) {
			creator = args[1];
			args = args.subspan(1);
		}
		else if ((args[0] == "-c"sv || args[0] == "--comment"sv) && args.size() > 1) {
			comment_str = args[1];
			args = args.subspan(1);
		}
		else if (args[0] == "-p"sv || args[0] == "--private"sv) {
			private_torrent = true;
		}
		else if ((args[0] == "-s"sv || args[0] == "--piece-size"sv) && args.size() > 1) {
			piece_size = atoi(args[1]);
			args = args.subspan(1);
			if (piece_size == 0) {
				std::cerr << "invalid piece size: \"" << args[1] << "\"\n";
				return 1;
			}
			if (piece_size < 16) {
				std::cerr << "piece size may not be smaller than 16 kiB\n";
				return 1;
			}
			if ((piece_size & (piece_size - 1)) != 0) {
				std::cerr << "piece size must be a power of 2 (specified " << piece_size << ")\n";
				return 1;
			}
			// convert kiB to Bytes
			piece_size *= 1024;
		}
		else if ((args[0] == "-r"sv || args[0] == "--root-cert"sv) && args.size() > 1) {
			root_cert = args[1];
			args = args.subspan(1);
		}
		else if (args[0] == "-q"sv) {
			quiet = true;
		}
		else if (args[0] == "-h"sv || args[0] == "--help"sv) {
			print_usage();
			return 0;
		}
		else if (args[0] == "-l"sv || args[0] == "--dont-follow-links"sv) {
			flags |= lt::create_torrent::symlinks;
		}
		else if (args[0] == "-2"sv || args[0] == "--v2-only"sv) {
			flags |= lt::create_torrent::v2_only;
		}
		else if (args[0] == "-m"sv || args[0] == "--mtime"sv) {
			flags |= lt::create_torrent::modification_time;
		}
		else {
			std::cerr << "unknown option (or missing argument) " << args[0] << '\n';
			print_usage();
			return 1;
		}
		args = args.subspan(1);
	}

	if (args.empty()) {
		print_usage();
		std::cerr << "no files specified.\n";
		return 1;
	}
	std::string full_path = args[0];

	lt::file_storage fs;
#ifdef TORRENT_WINDOWS
	if (full_path[1] != ':')
#else
	if (full_path[0] != '/')
#endif
	{
		char cwd[2048];
#ifdef TORRENT_WINDOWS
#define getcwd_ _getcwd
#else
#define getcwd_ getcwd
#endif

		char const* ret = getcwd_(cwd, sizeof(cwd));
		if (ret == nullptr) {
			std::cerr << "failed to get current working directory: "
				<< strerror(errno) << "\n";
			return 1;
		}

#undef getcwd_
#ifdef TORRENT_WINDOWS
		full_path = cwd + ("\\" + full_path);
#else
		full_path = cwd + ("/" + full_path);
#endif
	}

	lt::add_files(fs, full_path, file_filter, flags);
	if (fs.num_files() == 0) {
		std::cerr << "no files specified.\n";
		return 1;
	}

	lt::create_torrent t(fs, piece_size, flags);
	int tier = 0;
	if (!trackers.empty()) {
		for (auto const& tt : trackers) {
			for (auto const& url : tt) {
				t.add_tracker(url, tier);
			}
			++tier;
		}
	}

	for (std::string const& ws : web_seeds)
		t.add_url_seed(ws);

	for (auto const& n : dht_nodes)
		t.add_node(n);

	t.set_priv(private_torrent);

	lt::settings_pack sett;
	sett.set_int(lt::settings_pack::hashing_threads, num_threads);
	auto const num = t.num_pieces();
	lt::set_piece_hashes(t, branch_path(full_path), sett
		, [num, quiet] (lt::piece_index_t const p) {
			if (quiet) return;
			std::cout << "\r" << (p + lt::piece_index_t::diff_type{1}) << "/" << num;
			std::cout.flush();
		});

	if (!quiet) std::cerr << "\n";
	t.set_creator(creator.c_str());
	if (!comment_str.empty()) {
		t.set_comment(comment_str.c_str());
	}

	if (!root_cert.empty()) {
		if (!quiet) std::cout << "loading " << root_cert << '\n';
		std::vector<char> const pem = load_file(root_cert);
		t.set_root_cert(std::string(&pem[0], pem.size()));
	}

	// create the torrent and print it to stdout
	std::vector<char> torrent;
	lt::bencode(back_inserter(torrent), t.generate());

	std::fstream out;
	out.exceptions(std::ifstream::failbit);
	out.open(output_file.c_str(), std::ios_base::out | std::ios_base::binary);
	out.write(torrent.data(), int(torrent.size()));

	return 0;
}
catch (std::exception& e) {
	std::cerr << "ERROR: " << e.what() << "\n";
	return 1;
}
