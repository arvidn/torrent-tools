/*

Copyright (c) 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/bencode.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/sha1_hash.hpp" // for sha256_hash
#include "libtorrent/torrent_info.hpp"

#include "common.hpp"

#include <ctime>
#include <unordered_map>
#include <set>
#include <iostream>
#include <string_view>
#include <stdexcept>

using namespace std::string_view_literals;

namespace {

void print_usage()
{
	std::cout << R"(USAGE: torrent-merge [OPTIONS] files...
OPTIONS:
-o, --out <file>          Store the resulting torrent to the specified file.
                          If not specified "a.torrent" is used.
-n, --name <name>         Set the name of the new torrent. If not specified,
                          the name of the first torrent will be used
-h, --help                Show this message
-q                        Quiet, do not print log messages

Reads the torrent files, specified by "files..." and creates a new torrent
containing all files in all torrents. Any file found in more than one torrent
will only be included once in the output.

Only BitTorrent v2 torrent files are supported.
)";
}

std::vector<lt::sha256_hash> make_piece_layer(lt::span<char const> bytes)
{
	if (bytes.size() % lt::sha256_hash::size() != 0)
		throw std::runtime_error("invalid piece layer size");

	std::vector<lt::sha256_hash> ret;
	ret.reserve(bytes.size() / lt::sha256_hash::size());
	for (int i = 0; i < bytes.size(); i += lt::sha256_hash::size())
		ret.emplace_back(bytes.data() + i);

	return ret;
}

struct file_metadata
{
	std::string filename;

	// the piece size the piece_layer represents. We need to save this in case
	// the piece layer needs to be moved up to a larger piece size.
	int piece_size;

	std::int64_t file_size;

	// modication time of the file. 0 if not specified
	std::int64_t mtime;

	// file attributes
	lt::file_flags_t file_flags;

	// the piece hashes for this file
	// note that small files don't have a piece layer
	std::vector<lt::sha256_hash> piece_layer;
};

lt::sha256_hash merkle_pad(int blocks, int pieces)
{
	TORRENT_ASSERT(blocks >= pieces);
	lt::sha256_hash ret{};
	while (pieces < blocks)
	{
		lt::hasher256 h;
		h.update(ret);
		h.update(ret);
		ret = h.final();
		pieces *= 2;
	}
	return ret;
}

std::size_t merkle_num_leafs(std::size_t const blocks)
{
	TORRENT_ASSERT(blocks > 0);
	TORRENT_ASSERT(blocks <= std::numeric_limits<std::size_t>::max() / 2);
	// round up to nearest 2 exponent
	std::size_t ret = 1;
	while (blocks > ret) ret <<= 1;
	return ret;
}

} // anonymous namespace

int main(int argc_, char const* argv_[]) try
{
	lt::span<char const*> args(argv_, argc_);
	// strip executable name
	args = args.subspan(1);

	std::unordered_map<lt::sha256_hash, file_metadata> files;

	std::string output_file = "a.torrent";
	std::string name;
	std::string creator;
	std::string comment_str;
	std::time_t creation_date = 0;
	bool private_torrent = false;
	bool quiet = false;
	std::set<std::string> web_seeds;
	std::set<std::pair<std::string, int>> dht_nodes;

	std::vector<std::set<std::string>> trackers;

	if (args.empty()) {
		print_usage();
		return 1;
	}

	while (args.size() > 0 && args[0][0] == '-') {

		if ((args[0] == "-o"sv || args[0] == "--out"sv) && args.size() > 1) {
			output_file = args[1];
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
			std::cerr << "unknown option " << args[0] << '\n';
			print_usage();
			return 1;
		}
		args = args.subspan(1);
	}

	// all remaining strings in args are expected to be .torrent files to be
	// loaded

	int max_piece_size = 0;
	for (auto const filename : args) {

		if (!quiet) std::cout << "-> " << filename << "\n";
		lt::torrent_info t{std::string(filename)};
		lt::file_storage const& fs = t.files();

		if (name.empty()) name = fs.name();

		for (auto const& ae : t.trackers()) {
			if (ae.tier >= trackers.size())
				trackers.resize(std::size_t(ae.tier) + 1);

			trackers[ae.tier].insert(ae.url);
		}

		for (auto const& ws : t.web_seeds()) {
			if (ws.type != lt::web_seed_entry::url_seed) continue;
			web_seeds.insert(ws.url);
		}

		for (auto const& n : t.nodes()) {
			dht_nodes.insert(n);
		}

		if (creator.empty())
			creator = t.creator();

		if (comment_str.empty())
			comment_str = t.comment();

		creation_date = std::max(creation_date, t.creation_date());

		// TODO: pull CA cert out

		private_torrent |= t.priv();

		for (lt::file_index_t i : fs.file_range()) {

			if (fs.pad_file_at(i)) continue;

			lt::sha256_hash const root = fs.root(i);
			if (files.find(root) != files.end()) {
				if (!quiet) std::cout << "ignoring " << fs.file_name(i) << " (duplicate)\n";
				continue;
			}

			if (fs.file_flags(i) & lt::file_storage::flag_symlink) {
				if (!quiet) std::cout << "ignoring " << fs.file_name(i) << " (symlinks not supported)\n";
				continue;
			}

			// TODO: what to do about different files with the same name? They
			// are not allowed by the torrent format

			max_piece_size = std::max(t.piece_length(), max_piece_size);

			auto const piece_layer = t.piece_layer(i);

			file_metadata meta{std::string(fs.file_name(i))
				, t.piece_length()
				, fs.file_size(i)
				, fs.mtime(i)
				, fs.file_flags(i)
				, make_piece_layer(piece_layer)};
			files[root] = std::move(meta);

			if (!quiet) std::cout << "  " << root << ' ' << fs.file_size(i) << ' ' << fs.file_name(i) << '\n';
		}
	}

	if (!quiet) {
		std::cout << "piece size: " << max_piece_size << '\n';

		if (!dht_nodes.empty()) {
			std::cout << "DHT nodes:\n";
			for (auto const& n : dht_nodes) {
				std::cout << n.first << ":" << n.second << '\n';
			}
		}

		if (!web_seeds.empty()) {
			std::cout << "web seeds:\n";
			for (auto const& w : web_seeds) {
				std::cout << w << '\n';
			}
		}

		if (!trackers.empty()) {
			std::cout << "trackers:\n";
			int counter = 0;
			for (auto const& tier : trackers) {
				if (!tier.empty()) std::cout << " tier " << counter << '\n';
				++counter;
				for (auto const& url : tier) {
					std::cout << "  " << url << '\n';
				}
			}
		}

		if (!comment_str.empty()) {
			std::cout << "comment: " << comment_str << '\n';
		}

		if (!creator.empty()) {
			std::cout << "created by: " << creator << '\n';
		}

		if (private_torrent)
			std::cout << "private: Yes\n";
	}

	lt::entry torrent_e;
	auto& p_layers = torrent_e["piece layers"];
	auto& info_out = torrent_e["info"];
	info_out["meta version"] = 2;
	info_out["piece length"] = max_piece_size;
	info_out["name"] = name;
	if (private_torrent) info_out["private"] = 1;
	if (!creator.empty()) {
		torrent_e["created by"] = creator;
	}
	if (!comment_str.empty()) {
		torrent_e["comment"] = comment_str;
	}
	torrent_e["creation date"] = creation_date ? creation_date : std::time(nullptr);
	if (!trackers.empty()) {
		if (trackers.size() == 1 && trackers.front().size() == 1) {
			torrent_e["announce"] = *trackers.front().begin();
		}
		else {
			auto& tiers = torrent_e["announce"].list();
			for (auto& tt : trackers) {
				auto& tier = tiers.emplace_back();
				for (auto& url : tt) {
					tier.list().emplace_back(std::move(url));
				}
			}
		}
	}
	if (!web_seeds.empty()) {
		auto& ws = torrent_e["url-list"];
		if (web_seeds.size() == 1) {
			ws = *web_seeds.begin();
		}
		else {
			for (auto& url : web_seeds) {
				ws.list().emplace_back(std::move(url));
			}
		}
	}
	if (!dht_nodes.empty()) {
		auto& nodes = torrent_e["nodes"];
		for (auto const& n : dht_nodes) {
			auto& l = nodes.list();
			l.push_back(n.first);
			l.push_back(n.second);
		}
	}

	auto& file_tree = info_out["file tree"];

	for (auto& [root, f] : files) {
		auto& entry = file_tree[f.filename][""];
		entry["length"] = f.file_size;
		entry["pieces root"] = root;
		if (f.mtime != 0) {
			entry["mtime"] = f.mtime;
		}
		if (f.file_flags & lt::file_storage::flag_executable)
			entry["attr"].string() += 'x';

		if (f.file_flags & lt::file_storage::flag_hidden)
			entry["attr"].string() += 'h';

		if (f.piece_size != max_piece_size) {
			// in this case we need to combine some of the piece layer hashes to
			// raise them up to a higher level in the merkle tree
			lt::sha256_hash pad = merkle_pad(f.piece_size / 0x4000, 1);

			f.piece_layer.resize(merkle_num_leafs(f.piece_layer.size()), pad);

			while (f.piece_size < max_piece_size) {
				// reduce the piece layer by one level
				for (std::size_t i = 0; i < f.piece_layer.size(); i += 2) {
					auto const left = f.piece_layer[i];
					auto const right = f.piece_layer[i + 1];
					f.piece_layer[i / 2] = lt::hasher256().update(left).update(right).final();
				}
				pad = lt::hasher256().update(pad).update(pad).final();
				f.piece_layer.resize(f.piece_layer.size() / 2);
				f.piece_size *= 2;
			}

			// remove any remaining padding at the end
			while (!f.piece_layer.empty() && f.piece_layer.back() == pad)
				f.piece_layer.resize(f.piece_layer.size() - 1);
		}

		// not all files have piece lyers. Files that are just a single block
		// just have the block hash as the tree root
		if (!f.piece_layer.empty()) {
			std::string& pieces = p_layers[root.to_string()].string();

			pieces.clear();
			pieces.reserve(f.piece_layer.size() * lt::sha256_hash::size());
			for (auto& p : f.piece_layer)
				pieces.append(reinterpret_cast<const char*>(p.data()), p.size());
		}
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

