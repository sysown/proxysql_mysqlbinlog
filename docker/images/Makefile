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


IMAGE_PREFIX ?= proxysql/proxysql-mysqlbinlog

GIT_TAG := $(shell git describe --tags --abbrev=7 2>/dev/null)
GIT_SHA := $(shell git rev-parse --short=7 HEAD 2>/dev/null)

# 1. Strip leading 'v' from a semver tag (e.g. v0.0.2 -> 0.0.2)
VERS ?= $(shell echo "$(GIT_TAG)" | grep -Po '(?<=^v|^)[\d\.]+')
# 2. No semver? Use the tag/describe output as-is (lightweight or non-semver tags)
ifeq ($(VERS),)
  VERS = $(GIT_TAG)
endif
# 3. No tags at all? Fall back to short commit SHA
ifeq ($(VERS),)
  VERS = $(GIT_SHA)
endif


.PHONY: default
default: mysqlbinlog


.PHONY: cleanimg
cleanimg:
	docker images -a | grep '$(IMAGE_PREFIX)' | grep -v 'build-' | awk '{print $$3}' | sort | uniq | xargs -r docker rmi -f

.PHONY: cleanbuild
cleanbuild:
	rm -f mysqlbinlog-*/proxysql-mysqlbinlog* || true

.PHONY: cleanall
cleanall: cleanimg cleanbuild
	docker system prune -a
	docker buildx prune -a


.PHONY: mysqlbinlog
mysqlbinlog: $(sort $(wildcard mysqlbinlog-*))


.PHONY: mysqlbinlog-centos
mysqlbinlog-centos:
	make $(sort $(wildcard mysqlbinlog-centos*))

.PHONY: mysqlbinlog-debian
mysqlbinlog-debian:
	make $(sort $(wildcard mysqlbinlog-debian*))

.PHONY: mysqlbinlog-ubuntu
mysqlbinlog-ubuntu:
	make $(sort $(wildcard mysqlbinlog-ubuntu*))


# get latest version of each distro
LTSTCEN := $(shell echo $(wildcard mysqlbinlog-centos*) | sed 's/ /\n/g' | sort -V | tail -1)
LTSTDEB := $(shell echo $(wildcard mysqlbinlog-debian*) | sed 's/ /\n/g' | sort -V | tail -1)
LTSTUBU := $(shell echo $(wildcard mysqlbinlog-ubuntu*) | sed 's/ /\n/g' | sort -V | tail -1)
# latest version of each distro
LTSTDST := ${LTSTCEN} ${LTSTDEB} ${LTSTUBU}
# latest is latest debian
DFLTDST := ${LTSTDEB}
#PLTFRMS := linux/arm64/v8,linux/amd64
PLTFRMS := linux/amd64

.SILENT:
.PHONY: mysqlbinlog-*
mysqlbinlog-*: DIST=$(patsubst mysqlbinlog-%,%,$@)
mysqlbinlog-*: DISTN=$(shell echo ${DIST} | grep -Po '[a-z]+')
mysqlbinlog-*: TAGV=$(if $(findstring ${DIST},$(DFLTDST)),-t $(IMAGE_PREFIX):${VERS},)
mysqlbinlog-*: TAGL=$(if $(findstring ${DIST},$(DFLTDST)),-t $(IMAGE_PREFIX):latest,)
mysqlbinlog-*: TAGD=-t $(IMAGE_PREFIX):${DIST}
mysqlbinlog-*: TAGVD=-t $(IMAGE_PREFIX):${VERS}-${DIST}
mysqlbinlog-*: TAGDN=$(if $(findstring ${DIST},$(LTSTDST)),-t $(IMAGE_PREFIX):${DISTN},)
mysqlbinlog-*: TAGVDN=$(if $(findstring ${DIST},$(LTSTDST)),-t $(IMAGE_PREFIX):${VERS}-${DISTN},)
mysqlbinlog-*:
	rm -f ./$@/proxysql-mysqlbinlog*
	cp ../../binaries/proxysql-mysqlbinlog_*$(DIST)* ./$@/ 2>/dev/null; \
	cp ../../binaries/proxysql-mysqlbinlog-*$(DIST)* ./$@/ 2>/dev/null; \
	ls -1 ./$@/proxysql-mysqlbinlog* | head -1 | xargs -r true || { echo "ERROR: no package found for $@ in ../../binaries/"; ls ../../binaries/; false; }
	# pre-pull base image to avoid transient Docker Hub timeouts during build
	BASE_IMG=$$(grep -Po '(?<=^FROM ).*' ./$@/Dockerfile | head -1); \
	for i in 1 2 3; do \
		echo "pulling $$BASE_IMG (attempt $$i/3)"; \
		docker pull "$$BASE_IMG" && break; \
		[ $$i -lt 3 ] && sleep 5 || { echo "ERROR: failed to pull $$BASE_IMG after 3 attempts"; exit 1; }; \
	done
ifndef MULTIARCH
	# build for local only
	@echo "building native $@ for 'native' using package '$$(cd ./$@/; ls -1 proxysql-mysqlbinlog*)'"
	docker build ${NOCACHE} ${TAGV} ${TAGL} ${TAGD} ${TAGVD} ${TAGDN} ${TAGVDN} --pull $@ ${PUSH} ${SILENT}
endif
ifdef MULTIARCH
	# try building both amd64 and arm64, fallback to native
	@echo "building multiarch $@ for '$(PLTFRMS)' using package '$$(cd ./$@/; ls -1 proxysql-mysqlbinlog*)'"
	docker buildx build ${NOCACHE} ${TAGV} ${TAGL} ${TAGD} ${TAGVD} ${TAGDN} ${TAGVDN} --pull --platform ${PLTFRMS} $@ ${PUSH} ${SILENT} || \
	docker buildx build ${NOCACHE} ${TAGV} ${TAGL} ${TAGD} ${TAGVD} ${TAGDN} ${TAGVDN} --pull $@ ${PUSH} ${SILENT}
endif
	@echo 'tagged $@'


.PHONY: imglist
imglist:
	docker images | grep '$(IMAGE_PREFIX)' | grep -v 'build-' || true
