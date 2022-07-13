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
	docker images -a | grep 'proxysql/packaging-mysqlbinlog' | awk '{print $$3}' | sort | uniq | xargs -r docker rmi -f

.PHONY: cleanall
cleanall:
	docker system prune -a


.PHONY: build
build: $(wildcard build-*)


.PHONY: build-centos
build-centos:
	make $(wildcard build-centos*)

.PHONY: build-debian
build-debian:
	make $(wildcard build-debian*)

.PHONY: build-ubuntu
build-ubuntu:
	make $(wildcard build-ubuntu*)


.SILENT:
.PHONY: build-*
build-*: DIST=$(patsubst build-%,%,$@)
build-*:
	echo 'building $@'
ifndef MULTIARCH
	# build for local only
	docker build -t proxysql/packaging-mysqlbinlog:build-$(DIST) --pull $@ ${SILENT}
endif
ifdef MULTIARCH
	# try building both amd64 and arm64, fallback to native
#	docker buildx build -t proxysql/packaging-mysqlbinlog:build-$(DIST) --pull --platform linux/arm64/v8,linux/amd64 $@ --push ${SILENT} || \
#	docker buildx build -t proxysql/packaging-mysqlbinlog:build-$(DIST) --pull $@ --push ${SILENT}
	docker buildx build -t proxysql/packaging-mysqlbinlog:build-$(DIST) --pull $@ --push ${SILENT}
endif
	echo 'tagged $@'



.ONESHELL: images
images:
	echo 'Finished building'
	docker images | grep "proxysql/proxysql-mysqlbinlog"
