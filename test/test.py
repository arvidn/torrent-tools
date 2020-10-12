#!/usr/bin/env python3
# vim: set fileencoding=UTF-8 :

import unittest
import subprocess
import os
import itertools

def run(args):
	out = subprocess.check_output(args).decode('utf-8')
	print(' '.join(args))
	print(out)
	return out.strip().split('\n')

class TestNew(unittest.TestCase):

	@classmethod
	def setUpClass(cls):
		# create some test files
		try: os.mkdir('test-files')
		except: pass
		global test_files_
		global size_

		test_files_ = ['test-files/file-number-1', 'test-files/file-number-2', 'test-files/file-number-3']
		size_ = [16000, 32000, 300]

		for i in range(len(test_files_)):
			run(['dd', 'count=%d' % size_[i], 'if=/dev/random', 'of=%s' % test_files_[i]])

	def test_single_file(self):
		for f in test_files_:
			run(['./torrent-new', '-o', 'test.torrent', f])
			out = run(['./torrent-print', 'test.torrent'])

			self.assertEqual(out[5], 'name: %s' % os.path.split(f)[1])
			self.assertEqual(out[6], 'number of files: 1')
			self.assertTrue(out[8].endswith(os.path.split(f)[1]))

	def test_private(self):
		run(['./torrent-new', '--private', '-o', 'test.torrent', test_files_[0]])
		out = run(['./torrent-print', '--private', 'test.torrent'])

		self.assertEqual(out[0], 'private: yes')

	def test_multi_file(self):
		run(['./torrent-new', '-o', 'test.torrent', 'test-files'])
		out = run(['./torrent-print', '--name', 'test.torrent'])
		self.assertEqual(out[0], 'name: test-files')
		out = run(['./torrent-print', '--files', '--flat', 'test.torrent'])

		# strip out "files:"
		out = out[1:]
		names = []
		sizes = []
		for i in range(len(out)):
			names.append(out[i].strip().split(' ')[-1])
			sizes.append(int(out[i].strip().split(' ')[0]) / 512)

		self.assertEqual(names, test_files_)
		self.assertEqual(sizes, size_)

	def test_piece_size(self):
		for f in test_files_:
			run(['./torrent-new', '-o', 'test.torrent', '--piece-size', '64', f])
			out = run(['./torrent-print', '--piece-size', 'test.torrent'])
			self.assertEqual(out[0], 'piece size: 65536')

	def test_small_piece_size(self):
		# piece size must be at least 16 kiB
		with self.assertRaises(Exception):
			run(['./torrent-new', '-o', 'test.torrent', '--piece-size', '1', f])

	def test_invalid_piece_size(self):
		# piece size must be a power of two
		with self.assertRaises(Exception):
			run(['./torrent-new', '-o', 'test.torrent', '--piece-size', '17', f])

	def test_comment(self):
		for f in test_files_:
			run(['./torrent-new', '-o', 'test.torrent', '--comment', 'foobar', f])
			out = run(['./torrent-print', '--comment', 'test.torrent'])
			self.assertEqual(out[0], 'comment: foobar')

	def test_creator(self):
		for f in test_files_:
			run(['./torrent-new', '-o', 'test.torrent', '--creator', 'foobar', f])
			out = run(['./torrent-print', '--creator', 'test.torrent'])

			self.assertEqual(out[0], 'created by: foobar')

	def test_tracker(self):
		run(['./torrent-new', '--tracker', 'https://tracker.test/announce', '-o', 'test.torrent', 'test-files'])
		out = run(['./torrent-print', '--trackers', 'test.torrent'])

		# strip "trackers:"
		out = out[1:]
		self.assertEqual(out[0].strip(), '0: https://tracker.test/announce')

	def test_tracker_tiers(self):
		run(['./torrent-new', \
			'--tracker', 'https://tracker1a.test/announce', \
			'--tracker-tier', 'https://tracker1b.test/announce', \
			'--tracker', 'https://tracker2a.test/announce', \
			'--tracker-tier', 'https://tracker2b.test/announce', \
			'-o', 'test.torrent', 'test-files'])
		out = run(['./torrent-print', '--trackers', 'test.torrent'])

		# strip "trackers:"
		out = out[1:]
		# the order within a tier is not defined, so we sort it to be able to
		# compare reliably
		out[0:2] = sorted(out[0:2])
		out[2:4] = sorted(out[2:4])
		self.assertEqual(out[0].strip(), '0: https://tracker1a.test/announce')
		self.assertEqual(out[1].strip(), '0: https://tracker1b.test/announce')
		self.assertEqual(out[2].strip(), '1: https://tracker2a.test/announce')
		self.assertEqual(out[3].strip(), '1: https://tracker2b.test/announce')

	def test_mtime(self):
		run(['./torrent-new', '--mtime', '-o', 'test.torrent', 'test-files'])
		out = run(['./torrent-print', '--file-mtime', '--files', '--flat', 'test.torrent'])
		for l in out[1:]:
			ts = l.split(' test-files')[0].strip().split(' ', 2)[-1]
			# validate timestamp
			# example: 2021-01-02 20:57:24
			self.assertEqual(len(ts), 19)
			year = int(ts[0:4])
			self.assertTrue(year > 2020)
			self.assertEqual(ts[4], '-')
			month = int(ts[5:7])
			self.assertTrue(month <= 12)
			self.assertTrue(month >= 1)
			self.assertEqual(ts[7], '-')
			day = int(ts[8:10])
			self.assertTrue(day <= 31)
			self.assertTrue(day >= 1)
			self.assertEqual(ts[10], ' ')
			hour = int(ts[11:13])
			self.assertTrue(hour <= 23)
			self.assertTrue(hour >= 0)
			self.assertEqual(ts[13], ':')
			minute = int(ts[14:16])
			self.assertTrue(minute <= 59)
			self.assertTrue(minute >= 0)
			self.assertEqual(ts[16], ':')
			sec = int(ts[17:19])
			self.assertTrue(sec <= 60)
			self.assertTrue(sec >= 0)

	def test_web_seeds_multifile(self):
		run(['./torrent-new', '--web-seed', 'https://web.com/torrent', '-o', 'test.torrent', 'test-files'])
		out = run(['./torrent-print', '--web-seeds', 'test.torrent'])

		# strip "web-seeds:"
		out = out[1:]
		self.assertEqual(out[0].strip(), 'BEP19 https://web.com/torrent/')

	def test_web_seeds_singlefile(self):
		run(['./torrent-new', '--web-seed', 'https://web.com/file', '-o', 'test.torrent', 'test-files/file-number-1'])
		out = run(['./torrent-print', '--web-seeds', 'test.torrent'])

		# strip "web-seeds:"
		out = out[1:]
		self.assertEqual(out[0].strip(), 'BEP19 https://web.com/file')

	def test_v2_only(self):
		run(['./torrent-new', '--v2-only', '-o', 'test.torrent', 'test-files'])
		out = run(['./torrent-print', '--info-hash', 'test.torrent'])
		# example:
		# info hash: v2:
		# 7791118351f7f15fe3333d7a6f793337b698492f61ca821daddd22cd2a3c2c19
		self.assertNotIn('v1:', out[0])
		self.assertIn('v2:', out[0])

		run(['./torrent-new', '-o', 'test.torrent', 'test-files'])
		out = run(['./torrent-print', '--info-hash', 'test.torrent'])
		# example:
		# info hash: v1: b193774831b16fe487281987dcce7edda40767b5 v2:
		# 7791118351f7f15fe3333d7a6f793337b698492f61ca821daddd22cd2a3c2c19
		self.assertIn('v1:', out[0])
		self.assertIn('v2:', out[0])

	def test_dht_nodes(self):
		run(['./torrent-new', '--dht-node', 'router1.com', '6881', '-o', 'test.torrent', 'test-files'])
		out = run(['./torrent-print', '--dht-nodes', 'test.torrent'])
		# example:
		# nodes:
		# router1.com: 6881
		self.assertIn('nodes:', out[0])
		self.assertIn('router1.com: 6881', out[1])

# test_root_cert
# test_symlinks


class TestPrint(unittest.TestCase):

	def test_tree(self):
		run(['./torrent-new', '-o', 'test.torrent', 'bin'])

		for opt in ['--file-mtime', '--no-file-size', '--file-piece-range', '--file-roots', '--human-readable', '--file-offsets', '--no-file-attributes']:
			out = run(['./torrent-print', '--files', '--tree', '--no-colors'] + [opt] + ['test.torrent'])
			self.validate_tree(out)

	# makes sure the lines are correctly aligned
	def validate_tree(self, lines):
		self.assertEqual(lines[0], 'files:')
		lines = lines[1:]

		# cols indicates whether the given column is a vertical line or not
		width = len(max(lines, key=len))
		cols = [False] * width

		# on the first line there should be exactly one └
		self.assertEqual(lines[0].count('└'), 1)

		# every entry *may* be a directory, in which case a new vertical line
		# is allowed. Record the column index of such allowance here. This is
		# only valid for the next iteration through the loop and need to be
		# reset to None. Only one new vertical line may be started per row
		new_vertical = lines[0].index('└') + 2

		for l in lines[1:]:
			done = False
			for s in range(len(l)):
				if done:
					self.assertNotIn(l[s], '└├│')
					continue
				if cols[s]:
					self.assertIn(l[s], '└├│')
					# this terminates a vertical line
					if (l[s] == '└'):
						cols[s] = False
					if l[s] in '└├':
						new_vertical = s + 2
						done = True
					self.assertEqual(l[s+1], ' ')
				else:
					if s == new_vertical and l[s] == '├':
						new_vertical = s + 2
						cols[s] = True
						self.assertEqual(l[s+1], ' ')
						done = True
					elif s == new_vertical and l[s] == '└':
						new_vertical = s + 2
						self.assertEqual(l[s+1], ' ')
						done = True
					else:
						self.assertNotIn(l[s], '└├│')

if __name__ == '__main__':
    unittest.main()
