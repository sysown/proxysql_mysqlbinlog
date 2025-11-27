#!/usr/bin/make -f

NPROCS := $(shell nproc)
export MAKEFLAGS=-j ${NPROCS}

# default multiarch
#MULTIARCH := 1

ifdef NOMULTIARCH
override undefine MULTIARCH
endif

ifdef LOAD
override LOAD := --load
override undefine MULTIARCH
endif

ifdef PUSH
override PUSH := --push
endif

ifdef SILENT
override SILENT := -q
endif

ifdef NOCACHE
override NOCACHE := --no-cache
endif


GIT_VERSION=$(shell git describe --long --abbrev=7)
VER=$(shell git describe --long --abbrev=7 | cut -d- -f1)


.PHONY: default
default: mysqlbinlog


.PHONY: cleanimg
cleanimg:
	docker images -a | grep 'proxysql/proxysql-mysqlbinlog' | grep -v 'build-' | awk '{print $$3}' | sort | uniq | xargs -r docker rmi -f

.PHONY: cleanbuild
cleanbuild:
	rm -f mysqlbinlog-*/proxysql-mysqlbinlog* || true

.PHONY: cleanall
cleanall: cleanimg cleanbuild
	docker system prune -a
	docker buildx prune -a


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


DFLTDST := debian13
#PLTFRMS := linux/arm64/v8,linux/amd64
PLTFRMS := linux/amd64

.SILENT:
.PHONY: mysqlbinlog-*
mysqlbinlog-*: DIST=$(patsubst mysqlbinlog-%,%,$@)
mysqlbinlog-*: TAGV=$(if $(findstring $(DFLTDST),$@),-t proxysql/proxysql-mysqlbinlog:${VER},)
mysqlbinlog-*: TAGL=$(if $(findstring $(DFLTDST),$@),-t proxysql/proxysql-mysqlbinlog:latest,)
mysqlbinlog-*: TAGVD=-t proxysql/proxysql-mysqlbinlog:${VER}-${DIST}
mysqlbinlog-*: TAGLD=-t proxysql/proxysql-mysqlbinlog:latest-${DIST}
mysqlbinlog-*:
	rm -f ./$@/proxysql-mysqlbinlog*
	cp ../../binaries/proxysql-mysqlbinlog*${VER}*$(DIST)* ./$@/
ifndef MULTIARCH
	# build for local only
	@echo "building native $@ for 'native'"
	docker build ${NOCACHE} ${TAGV} ${TAGL} ${TAGVD} ${TAGLD} --pull $@ ${PUSH} ${SILENT}
endif
ifdef MULTIARCH
	# try building both amd64 and arm64, fallback to native
	@echo "building multiarch $@ for '$(PLTFRMS)'"
	docker buildx build ${NOCACHE} ${TAGV} ${TAGL} ${TAGVD} ${TAGLD} --pull --platform ${PLTFRMS} $@ ${PUSH} ${SILENT} || \
	docker buildx build ${NOCACHE} ${TAGV} ${TAGL} ${TAGVD} ${TAGLD} --pull $@ ${PUSH} ${SILENT}
endif
	@echo 'tagged $@'


.PHONY: imglist
imglist:
	docker images | grep 'proxysql/proxysql-mysqlbinlog' | grep -v 'build-' || true
