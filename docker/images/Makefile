#!/usr/bin/make -f

NPROCS := $(shell nproc)
export MAKEFLAGS=-j ${NPROCS}

ifdef SILENT
override SILENT=-q
endif

VER=$(shell git tag | sort -r | head -1)


.PHONY: default
default: mysqlbinlog

.PHONY: clean
clean:
	docker images -a | grep "proxysql/proxysql-mysqlbinlog" | awk '{print $3}' | xargs -r 'docker rmi | grep "proxysql/proxysql-mysqlbinlog"'

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
	cp ../../binaries/proxysql-mysqlbinlog*$(DIST)* ./$@/
ifndef MULTIARCH
	# build for local only
	docker build -t proxysql/proxysql-mysqlbinlog:$(TAGV) -t proxysql/proxysql-mysqlbinlog:$(TAGL) --pull $@ ${SILENT}
endif
ifdef MULTIARCH
	# try building both amd64 and arm64, fallback to amd64
	docker buildx build -t proxysql/proxysql-mysqlbinlog:$(TAGV) -t proxysql/proxysql-mysqlbinlog:$(TAGL) --pull --platform linux/arm64/v8,linux/amd64 $@ --push ${SILENT} || \
	docker buildx build -t proxysql/proxysql-mysqlbinlog:$(TAGV) -t proxysql/proxysql-mysqlbinlog:$(TAGL) --pull --platform linux/amd64 $@ --push ${SILENT}
endif
	echo 'tagged $@'



.ONESHELL: images
images:
	echo 'Finished building'
	docker images | grep "proxysql/proxysql-mysqlbinlog"