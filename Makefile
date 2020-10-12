BUILD_CONFIG=release cxxstd=17 link=shared crypto=openssl libtorrent-link=system warnings=off address-model=64

ifeq (${PREFIX},)
PREFIX=/usr/local/
endif

ALL: FORCE
	BOOST_ROOT="" b2 ${BUILD_CONFIG} stage

install: FORCE
	BOOST_ROOT="" b2 ${BUILD_CONFIG} install --prefix=${PREFIX}

clean: FORCE
	rm -rf bin

check: ALL
	python test/test.py

FORCE:
