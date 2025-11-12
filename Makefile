#!/bin/make -f


### NOTES:
### version string is fetched from git history
### when not available, specify GIT_VERSION on commnad line:
###
### ```
### export GIT_VERSION=2.x.y-dev
### ```

GIT_VERSION ?= $(shell git describe --long --abbrev=7 2>/dev/null)
ifeq ($(GIT_VERSION),)
    $(error GIT_VERSION is not set)
endif
export GIT_VERSION

### NOTES:
### SOURCE_DATE_EPOCH is used for reproducible builds
### for details consult https://reproducible-builds.org/docs/source-date-epoch/

SOURCE_DATE_EPOCH ?= $(shell git show -s --format=%ct HEAD || date +%s)
export SOURCE_DATE_EPOCH


# include paths
IDIRS :=	-I./libslave \
			-I./libev \
			-I./libdaemon

# link paths
LDIRS :=	-L/usr/lib64/mysql

# link archives
DEPS :=		./libslave/libslave.a \
			./libev/.libs/libev.a \
			./libdaemon/libdaemon/.libs/libdaemon.a


# build targets

.PHONY: default
default: proxysql_binlog_reader


proxysql_binlog_reader: libev libdaemon libslave
	@$(CXX) -o proxysql_binlog_reader proxysql_binlog_reader.cpp -std=c++11 -DGITVERSION=\"$(GIT_VERSION)\" -ggdb $(DEPS) $(IDIRS) $(LDIRS) -rdynamic -lz -ldl -lssl -lcrypto -lpthread -lboost_system -lrt -Wl,-Bstatic -lmysqlclient -Wl,-Bdynamic -ldl -lssl -lcrypto -pthread
# -lperconaserverclient if compiled with percona server

libev/.libs/libev.a:
	rm -rf libev-*/ || true
	tar -zxf libev-4.24.tar.gz
	cd libev-4.24 && ./configure
	cd libev && CC=${CC} CXX=${CXX} ${MAKE}
libev: libev/.libs/libev.a

libdaemon/libdaemon/.libs/libdaemon.a:
	rm -rf libdaemon-*/ || true
	tar -zxf libdaemon-0.14.tar.gz
	cd libdaemon && ./configure --disable-examples
	cd libdaemon && CC=${CC} CXX=${CXX} ${MAKE}
libdaemon: libdaemon/libdaemon/.libs/libdaemon.a

libslave/libslave.a:
	rm -rf libslave-*/ || true
	tar -zxf libslave-20171226.tar.gz
	# Enable for allowing other replication formats (STATEMENT, MIXED) for debugging purposes
	#patch -p0 < patches/slave_allow_rep_formats.patch
	patch -p0 < patches/libslave_DBUG_ASSERT.patch
	patch -p0 < patches/libslave_ER_MALFORMED_GTID_SET_ENCODING.patch
	patch -p0 < patches/libslave_SSL_MODE_DISABLED.patch
	patch -p0 < patches/libslave_new_binlog_events.patch
	patch -p0 < patches/libslave_show_master_status_deprecated.patch
	cd libslave && cmake .
	cd libslave && make slave_a
libslave: libslave/libslave.a


### packaging targets

SYS_KERN := $(shell uname -s)
#SYS_DIST := $(shell source /etc/os-release &>/dev/null; if [ -z ${NAME} ]; then head -1 /etc/redhat-release; else echo ${NAME}; fi | awk '{ print $1 })
SYS_ARCH := $(shell uname -m)
REL_ARCH = $(subst x86_64,amd64,$(subst aarch64,arm64,$(SYS_ARCH)))
RPM_ARCH = .$(SYS_ARCH)
DEB_ARCH = _$(REL_ARCH)
GIT_VERS := ${GIT_VERSION}
REL_VERS := $(shell echo ${GIT_VERSION} | grep -Po '(?<=^v|^)[\d\.]+')
RPM_VERS := -$(REL_VERS)-1
DEB_VERS := _$(REL_VERS)

build: packages
packages: $(REL_ARCH)-packages ;
centos: $(REL_ARCH)-centos ;
debian: $(REL_ARCH)-debian ;
ubuntu: $(REL_ARCH)-ubuntu ;
pkglist: $(REL_ARCH)-pkglist

amd64-%: SYS_ARCH := x86_64
amd64-packages: amd64-centos amd64-debian amd64-ubuntu
amd64-centos: build-centos7 build-centos8 build-centos9 build-centos10
amd64-debian: build-debian9 build-debian10 build-debian11 build-debian12 build-debian13
amd64-ubuntu: build-ubuntu16 build-ubuntu18 build-ubuntu20 build-ubuntu22 build-ubuntu24
amd64-pkglist:
	@${MAKE} -nk amd64-packages 2>/dev/null | grep -Po '(?<=binaries/)proxysql_binlog_reader\S+$$'

centos%: build-centos% ;
debian%: build-debian% ;
ubuntu%: build-ubuntu% ;


# universal distro target
.PHONY: build-%
.NOTPARALLEL: build-%
build-%: BLD_NAME=$(patsubst build-%,%,$@)
build-%: PKG_VERS=$(if $(filter $(shell echo ${BLD_NAME} | grep -Po '[a-z]+'),debian ubuntu),$(DEB_VERS),$(RPM_VERS))
build-%: PKG_TYPE=$(if $(filter $(shell echo $(BLD_NAME) | grep -Po '\-de?bu?g|\-test|\-tap'),-dbg -debug -test -tap),-dbg,)
build-%: PKG_DIST=$(firstword $(subst -, ,$(BLD_NAME)))
build-%: PKG_COMP=$(if $(filter $(shell echo $(BLD_NAME) | grep -Po '\-clang'),-clang),-clang,)
build-%: PKG_ARCH=$(if $(filter $(shell echo ${BLD_NAME} | grep -Po '[a-z]+'),debian ubuntu),$(DEB_ARCH),$(RPM_ARCH))
build-%: PKG_KIND=$(if $(filter $(shell echo ${BLD_NAME} | grep -Po '[a-z]+'),debian ubuntu),deb,rpm)
build-%: PKG_FILE=binaries/proxysql_binlog_reader$(PKG_VERS)$(PKG_TYPE)-$(PKG_DIST)$(PKG_COMP)$(PKG_ARCH).$(PKG_KIND)
build-%:
	echo 'building $@'
	@IMG_NAME=$(PKG_DIST) PKG_DIST=$(PKG_DIST) PKG_KIND=$(PKG_KIND) PKG_VERS=$(REL_VERS) GIT_VERS=$(GIT_VERSION) $(MAKE) $(PKG_FILE)

.NOTPARALLEL: binaries/proxysql_binlog_reader%
binaries/proxysql_binlog_reader%:
	${MAKE} cleanbuild
	find . -not -path "./binaries/*" -not -path "./.git/*" | xargs touch -h --date=@${SOURCE_DATE_EPOCH}
	@docker compose -p "mysqlbinlog-${GIT_VERSION/./}" down -v --remove-orphans
	@docker compose -p "mysqlbinlog-${GIT_VERSION/./}" up mysqlbinlog
	@docker compose -p "mysqlbinlog-${GIT_VERSION/./}" down -v --remove-orphans


# clean targets

.PHONY: cleanbuild
cleanbuild:
	rm -f proxysql_binlog_reader || true
	rm -f proxysql-mysqlbinlog* || true
	rm -rf libev-*/
	rm -rf libslave-*/
	rm -rf libslave-*/
	find . -name '*.a' -delete
	find . -name '*.o' -delete

.PHONY: cleanall
cleanall: cleanbuild
	rm -rf binaries/*
