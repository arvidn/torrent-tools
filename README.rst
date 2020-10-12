torrent tools
=============

This is a collection of tools for creating and manipulating BitTorrent v2
torrent files. ``torrent-new`` can create hybrid torrents, but the other tools
for manipulating torrents only supports v2.

torrent-new
	Creates new torrent files

torrent-merge
	merges multiple torrents and creates a new torrent with all files in it

torrent-add
	add a new file to an existing torrent

torrent-modify
	remove and rename files from a torrent. Add and remove trackers, DHT nodes,
	web sedds and other properties.

torrent-print
	print the content of a .torrent file to stdout

examples
========

Here's a demonstration of some of the things one can do with these tools. First,
create two files to test::

	$ dd count=16000 if=/dev/random of=file-number-1
	$ dd count=32000 if=/dev/random of=file-number-2

create a torrent
----------------

We can create a torrent from one of them (this is a v2-only torrent)::

	$ ./torrent-new -o torrent-1.torrent -2 -m file-number-1
	/Users/arvid/Documents/dev/torrent-tools/file-number-1
	249/250

See what it looks like::

	$ ./torrent-print torrent-1.torrent
	piece-count: 250
	piece length: 32768
	info hash: v2: 9a12b62cf2717146eeaf5519cd5fdc981a251fd31df1f92d2a664f5320513ec2
	comment:
	created by: torrent-tools
	name: file-number-1
	number of files: 1
	files:
	    8192000 ---- file-number-1

extend a torrent
----------------

Now, we can extend torrent-1.torrent, by adding another file to it. This is done
without re-hashing ``file-number-1``, just the file that's being added::

	$ ./torrent-add torrent-1.torrent -o torrent-2.torrent -m file-number-2
	piece size: 32768
	adding file-number-2
	499/500
	-> writing to torrent-2.torrent

The new torrent looks like this::

	$ ./torrent-print torrent-2.torrent
	piece-count: 750
	piece length: 32768
	info hash: v2: 982f09b470be0e2218147b0a1aa2fb24c42cfde2c6fbb922fcbf3eba1b9089bb
	comment:
	created by: torrent-tools
	name: file-number-1
	number of files: 2
	files:
	    8192000 ---- file-number-1/file-number-1
	   16384000 ---- file-number-1/file-number-2

merge torrents
--------------

Create two torrents, one file each::

	$ ./torrent-new -o 1.torrent -2 -m file-number-1
	/Users/arvid/Documents/dev/torrent-tools/file-number-1
	249/250
	$ ./torrent-new -o 2.torrent -2 -m file-number-2
	/Users/arvid/Documents/dev/torrent-tools/file-number-2
	249/250

Merge them::

	$ ./torrent-merge -o merged.torrent 1.torrent 2.torrent
	-> 1.torrent
	  8545cbbe942e7c0d8061a8ed31b0badb32feadc1f4963ec2f6643df30d07500b 8192000 file-number-1
	-> 2.torrent
	  d64405bd94ec88f708481612a1dd35203d88d864191b0a584e2ebc27cfa336dd 16384000 file-number-2
	new piece size: 65536
	-> writing to merged.torrent

The new torrent now contains both files::

	$ ./torrent-print merged.torrent
	piece-count: 375
	piece length: 65536
	info hash: v2: a2898893986df997ee10c7765a8ae8b523460e18363b89c8c6ba15070b9c985c
	comment:
	created by: torrent-tools
	name: file-number-1
	number of files: 2
	files:
	    8192000 ---- file-number-1/file-number-1
	   16384000 ---- file-number-1/file-number-2

modify torrents
---------------

The ``merged.torrent`` has a root directory called ``file-number-1``::

	$ ./torrent-print --tree merged.torrent
	piece-count: 375
	piece length: 65536
	info hash: v2: a2898893986df997ee10c7765a8ae8b523460e18363b89c8c6ba15070b9c985c
	comment: 
	created by: torrent-tools
	name: file-number-1
	number of files: 2
	files:
	                 └ file-number-1
	    8192000 ----   ├ file-number-1
	   16384000 ----   └ file-number-2

Let's rename the root folder to "foobar"::

	$ ./torrent-modify --name foobar -o merged2.torrent merged.torrent

The new torrent looks like this::

	$ ./torrent-print --tree merged2.torrent
	piece-count: 375
	piece length: 65536
	info hash: v2: b2adfd009b31bc56f7af9f9962fc9fdc1d80282e598e8d8857c7e82aa9b55cd4
	comment: 
	created by: torrent-tools
	name: foobar
	number of files: 2
	files:
	                 └ foobar
	    8192000 ----   ├ file-number-1
	   16384000 ----   └ file-number-2

Now, set a comment::

	$ ./torrent-modify --comment "This is a foobar" -o merged3.torrent merged2.torrent

The new torrent looks like this::

	$ ./torrent-print --tree merged3.torrent
	piece-count: 375
	piece length: 65536
	info hash: v2: b2adfd009b31bc56f7af9f9962fc9fdc1d80282e598e8d8857c7e82aa9b55cd4
	comment: This is a foobar
	created by: torrent-tools
	name: foobar
	number of files: 2
	files:
	                 └ foobar
	    8192000 ----   ├ file-number-1
	   16384000 ----   └ file-number-2

