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
default: build


.PHONY: cleanimg
cleanimg:
	docker images -a | grep 'proxysql/packaging-mysqlbinlog' | grep 'build-' | awk '{print $$3}' | sort | uniq | xargs -r docker rmi -f

.PHONY: cleanall
cleanall:
	docker system prune -a
	docker buildx prune -a


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


#PLTFRMS := linux/arm64/v8,linux/amd64
PLTFRMS := linux/amd64

.SILENT:
.PHONY: build-*
build-*: DIST=$(patsubst build-%,%,$@)
build-*: TAGD=-t proxysql/proxysql-mysqlbinlog:build-$(DIST)
build-*: TAGDV=-t proxysql/proxysql-mysqlbinlog:build-$(DIST)-v$(VER)
build-*:
ifndef MULTIARCH
	# build for local only
	@echo "building native $@ for 'native'"
	docker build ${NOCACHE} ${TAGD} ${TAGDV} --pull $@ ${PUSH} ${SILENT}
endif
ifdef MULTIARCH
	# try building both amd64 and arm64, fallback to native
	@echo "building multiarch $@ for '$(PLTFRMS)'"
	docker buildx build ${NOCACHE} ${TAGD} ${TAGDV} --pull --platform ${PLTFRMS} $@ ${PUSH} ${SILENT} || \
	docker buildx build ${NOCACHE} ${TAGD} ${TAGDV} --pull $@ ${PUSH} ${SILENT}
endif
	@echo 'tagged $@'


.PHONY: imglist
imglist:
	docker images | grep 'proxysql/proxysql-mysqlbinlog' | grep 'build-' || true
