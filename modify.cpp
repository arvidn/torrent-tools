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

#include "common.hpp"

#include <functional>
#include <cstdio>
#include <sstream>
#include <fstream>
#include <iostream>
#include <set>

#ifdef TORRENT_WINDOWS
#include <direct.h> // for _getcwd
#endif

#include <string_view>

using namespace std::string_view_literals;

namespace {

using namespace std::placeholders;

void print_usage()
{
	std::cerr << R"(USAGE: torrent-modify [OPTIONS] file

Loads the specified torrent file, modifies it according to the specified options
and writes it to an output .torrent file (as specified by -o)

OPTIONS:
-o, --out <file>          Print resulting torrent to the specified file.
                          If not specified "a.torrent" is used.

adding fields:

-n, --name <name>             Change name of the torrent to the specified one. This
                              also affects the name of the root directory.
-t, --tracker <url>           Add <url> as a tracker in a new tier.
-T, --tracker-tier <url>      Add <url> as a tracker in the current tier.
-w, --web-seed <url>          Add <url> as a web seed to the torrent.
-C, --creator <name>          sets the "created by" field to <name>.
-c, --comment <str>           Sets the "comment" field to <str>.
-d, --dht-node <host> <port>  Add a DHT node with the specified hostname and port.
--private                     Set the "private" field to 1.
--root-cert <file>            Embed the specified root certificate in the torrent file
                              (for SSL torrents only). All peers and trackers must
                              authenticate with a cert signed by this root, directly
                              or indirectly.

Removing fields:

--public                      Remove the "private" flag
--drop-mtime                  Remove all mtime fields from files
--drop-trackers               Remove all trackers (this happens before any new
                              trackers are added from the command line)
--drop-web-seeds              Remove all web seeds (this happens before any new web
                              seeds are added from the command line)
--drop-dht-nodes              Remove DHT nodes from the torrent file (new DHT nodes
                              can still be added with the --dht-node option)
--drop-comment                Remove comment
--drop-creator                Remove creator string
--drop-creation-date          Remove creation date field
--drop-root-cert              Remove the root certificate.

Removing files:

--drop-file <name>            Remove all files whose name exactly matches <name>

-h, --help                    Show this message

TRACKER TIERS

To manage tracker tiers -t will add a new tier immediately before adding the
tracker whereas -T will add the tracker to the current tier. If there is no
tier, one will be created regardless of which flavour of -t and -T is used. e.g.

  -t https://foo.com -t https://bar.com

Will add foo and bar as separate tiers.

  -t https://foo.com -T https://bar.com

Will add foo and bar as the same tier.
)";
}

struct file_metadata
{
	lt::index_range<lt::piece_index_t> pieces;
	lt::span<char const> piece_layer;
	char const* root_hash;
	lt::file_index_t idx;
};

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

	std::string creator;
	std::string name;
	std::string comment_str;
	bool make_private_torrent = false;
	bool make_public_torrent = false;
	std::vector<std::string> web_seeds;
	std::vector<std::vector<std::string>> trackers;
	std::vector<std::pair<std::string, int>> dht_nodes;
	lt::create_flags_t flags = {};
	std::string root_cert;
	bool quiet = false;
	std::set<std::string> drop_file;
	std::map<std::string, std::string> rename_file;

	bool drop_trackers = false;
	bool drop_mtime = false;
	bool drop_web_seeds = false;
	bool drop_dht_nodes = false;
	bool drop_comment = false;
	bool drop_creator = false;
	bool drop_creation_date = false;
	bool drop_root_cert = false;

	std::string output_file = "a.torrent";

	while (args.size() > 0 && args[0][0] == '-') {

		if ((args[0] == "-o"sv || args[0] == "--out"sv) && args.size() > 1) {
			output_file = args[1];
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
		else if (args[0] == "--drop-file"sv && args.size() > 1) {
			drop_file.emplace(args[1]);
			args = args.subspan(1);
		}
		else if (args[0] == "--rename-file"sv && args.size() > 2) {
			rename_file.emplace(args[1], args[2]);
			args = args.subspan(2);
		}
		else if (args[0] == "--drop-trackers"sv) {
			drop_trackers = true;
		}
		else if (args[0] == "--drop-mtime"sv) {
			drop_mtime = true;
		}
		else if (args[0] == "--drop-web-seeds"sv) {
			drop_web_seeds = true;
		}
		else if (args[0] == "--drop-dht-nodes"sv) {
			drop_dht_nodes = true;
		}
		else if (args[0] == "--drop-comment"sv) {
			drop_comment = true;
		}
		else if (args[0] == "--drop-creator"sv) {
			drop_creator = true;
		}
		else if (args[0] == "--drop-creation-date"sv) {
			drop_creation_date = true;
		}
		else if (args[0] == "--drop-root-cert"sv) {
			drop_root_cert = true;
		}
		else if (args[0] == "--private"sv) {
			make_private_torrent = true;
		}
		else if (args[0] == "--public"sv) {
			make_public_torrent = true;
		}
		else if ((args[0] == "-r"sv || args[0] == "--root-cert"sv) && args.size() > 1) {
			std::string cert_path = args[1];

			if (!quiet) std::cout << "loading " << cert_path << '\n';
			std::vector<char> const pem = load_file(cert_path);
			root_cert.assign(pem.data(), pem.size());
			args = args.subspan(1);
		}
		else if (args[0] == "-q"sv) {
			quiet = true;
		}
		else if (args[0] == "-h"sv || args[0] == "--help"sv) {
			print_usage();
			return 0;
		}
		else if ((args[0] == "-n"sv || args[0] == "--name"sv) && args.size() > 1) {
			name = args[1];
			args = args.subspan(1);
		}
		else {
			std::cerr << "unknown option (or missing argument) " << args[0] << '\n';
			print_usage();
			return 1;
		}
		args = args.subspan(1);
	}

	if (make_public_torrent && make_private_torrent) {
		std::cerr << "the flags --public and --private are incompatible\n";
		print_usage();
		return 1;
	}

	if (args.empty()) {
		print_usage();
		std::cerr << "no torrent file specified.\n";
		return 1;
	}
	std::string full_path = args[0];

	if (args.size() > 1) {
		print_usage();
		std::cerr << "ignored command line arguments after input file\n";
		return 1;
	}

	lt::torrent_info input(full_path);
	lt::file_storage const& input_fs = input.files();

	// the new file storage
	lt::file_storage fs;
	std::vector<file_metadata> file_info;

	int const piece_size = input.piece_length();
	fs.set_piece_length(piece_size);

	for (auto f : input_fs.file_range()) {

		lt::file_flags_t const file_flags = input_fs.file_flags(f);
		if (file_flags & lt::file_storage::flag_pad_file) continue;

		std::int64_t const file_offset = input_fs.file_offset(f);
		if ((file_offset % piece_size) != 0) {
			std::cerr << "file " << f << " (" << input_fs.file_name(f) << ") is not piece-aligned\n";
			return 1;
		}

		std::string path = input_fs.file_path(f);
		std::int64_t const file_size = input_fs.file_size(f);
		std::time_t const mtime = drop_mtime ? 0 : input_fs.mtime(f);
		std::string const symlink_path
			= file_flags & lt::file_storage::flag_symlink
			? input_fs.symlink(f) : std::string();
		char const* root_hash = input_fs.root_ptr(f);

		auto const [parent, filename] = right_split(path);

		// ignore files whose name match one in drop_file
		if (!drop_file.empty()) {
			if (drop_file.count(filename))
				continue;
		}

		if (!name.empty()) {
			path = replace_directory_element(path, name);
		}

		if (auto it = rename_file.find(filename); it != rename_file.end()) {
#ifdef TORRENT_WINDOWS
			path = parent + '\\' + it->second;
#else
			path = parent + '/' + it->second;
#endif
		}

		fs.add_file(path, file_size, file_flags, mtime, symlink_path, root_hash);
		file_info.push_back(file_metadata{
			lt::index_range<lt::piece_index_t>{
				lt::piece_index_t(file_offset / piece_size)
				, lt::piece_index_t((file_offset + input_fs.file_size(f) + piece_size - 1) / piece_size)}
			, input.piece_layer(f)
			, root_hash
			, --fs.end_file()
			});
	}

	lt::create_torrent t(fs, piece_size, flags);

	// comment
	if (!drop_comment && comment_str.empty())
		comment_str = input.comment();

	if (!comment_str.empty())
		t.set_comment(comment_str.c_str());

	// creator
	if (!drop_creator && creator.empty())
		creator = input.creator();

	if (!creator.empty())
		t.set_creator(creator.c_str());

	if (drop_creation_date) {
		t.set_creation_date(0);
	}
	else {
		t.set_creation_date(input.creation_date());
	}

	// SSL root cert
	if (!drop_root_cert && root_cert.empty()) {
		root_cert = std::string(input.ssl_cert());
	}

	if (!root_cert.empty()) {
		t.set_root_cert(root_cert);
	}

	// propagate trackers
	if (!drop_trackers) {
		for (auto const& tr : input.trackers()) {
			int const tier = tr.tier;
			if (trackers.size() <= tier) trackers.resize(tier + 1);
			trackers[tier].emplace_back(tr.url);
		}
	}

	int tier = 0;
	if (!trackers.empty()) {
		for (auto const& tt : trackers) {
			for (auto const& url : tt) {
				t.add_tracker(url, tier);
			}
			++tier;
		}
	}

	// propagate web seeds
	if (!drop_web_seeds) {
		for (auto const& ws : input.web_seeds())
			web_seeds.emplace_back(ws.url);
	}
	for (std::string const& ws : web_seeds)
		t.add_url_seed(ws);

	// DHT nodes
	if (!drop_dht_nodes) {
		auto const& input_nodes = input.nodes();
		dht_nodes.insert(dht_nodes.end(), input_nodes.begin(), input_nodes.end());
	}
	for (auto const& n : dht_nodes) {
		t.add_node(n);
	}

	// propagate private flag
	if (make_private_torrent)
		t.set_priv(true);
	else if (make_public_torrent)
		t.set_priv(false);
	else
		t.set_priv(input.priv());

	if (input.info_hashes().has_v1()) {
		lt::piece_index_t p{};
		for (auto const& info : file_info) {
			for (lt::piece_index_t i : info.pieces) {
				t.set_hash(p++, input.hash_for_piece(i));
			}
		}
	}

	if (input.info_hashes().has_v2()) {
		for (auto const& info : file_info) {
			lt::piece_index_t::diff_type p{0};
			for (int h = 0; h < int(info.piece_layer.size()); h += int(lt::sha256_hash::size())) {
				t.set_hash2(info.idx, p++, lt::sha256_hash(info.piece_layer.data() + h));
			}
		}
	}

	// create the torrent and print it
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

