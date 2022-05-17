#!/usr/bin/make -f

NPROCS := $(shell nproc)
export MAKEFLAGS=-j ${NPROCS}

ifdef SILENT
override SILENT=-q
endif

GIT_VERSION=$(shell git describe --long --abbrev=7)
VER=$(shell git describe --long --abbrev=7 | cut -d- -f1)


.PHONY: default
default: mysqlbinlog

.PHONY: clean
clean:
	docker images -a | grep 'proxysql/proxysql-mysqlbinlog' | awk '{print $$3}' | sort | uniq | xargs -r docker rmi -f

.PHONY: cleanall
cleanall:
	docker system prune -a


.PHONY: mysqlbinlog
mysqlbinlog: $(wildcard mysqlbinlog-*)


.PHONY: mysqlbinlog-centos
mysqlbinlog-centos:
	make $(wildcard mysqlbinlog-centos*)

.PHONY: mysqlbinlog-debian
mysqlbinlog-debian:
	make $(wildcard mysqlbinlog-debian*)

.PHONY: mysqlbinlog-ubuntu
mysqlbinlog-ubuntu:
	make $(wildcard mysqlbinlog-ubuntu*)


.SILENT:
.PHONY: mysqlbinlog-*
mysqlbinlog-*: DIST=$(patsubst mysqlbinlog-%,%,$@)
mysqlbinlog-*: TAGV=$(shell echo "${VER}-${DIST}")
mysqlbinlog-*: TAGL=$(shell echo "latest-${DIST}")
mysqlbinlog-*:
	echo 'building $@'
	rm -f ./$@/proxysql-mysqlbinlog*$(DIST)*
	cp ../../binaries/proxysql-mysqlbinlog*$(DIST)* ./$@/
ifndef MULTIARCH
	# build for local only
	docker build -t proxysql/proxysql-mysqlbinlog:$(TAGV) -t proxysql/proxysql-mysqlbinlog:$(TAGL) --pull $@ ${SILENT}
endif
ifdef MULTIARCH
	# try building both amd64 and arm64, fallback to native
#	docker buildx build -t proxysql/proxysql-mysqlbinlog:$(TAGV) -t proxysql/proxysql-mysqlbinlog:$(TAGL) --pull --platform linux/arm64/v8,linux/amd64 $@ --push ${SILENT} || \
#	docker buildx build -t proxysql/proxysql-mysqlbinlog:$(TAGV) -t proxysql/proxysql-mysqlbinlog:$(TAGL) --pull $@ --push ${SILENT}
	docker buildx build -t proxysql/proxysql-mysqlbinlog:$(TAGV) -t proxysql/proxysql-mysqlbinlog:$(TAGL) --pull $@ --push ${SILENT}
endif
	echo 'tagged $@'



.ONESHELL: images
images:
	echo 'Finished building'
	docker images | grep "proxysql/proxysql-mysqlbinlog"
