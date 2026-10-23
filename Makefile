SHELL := /usr/bin/env bash
.DEFAULT_GOAL := help

RETRO68_IMAGE ?= ghcr.io/autc04/retro68:latest

.PHONY: help build clean

help:
	@printf '%s\n' \
	  'Jellyfin 68K - native music client for classic 68K Macintosh' \
	  '' \
	  '  make build   Cross-compile the client into build/Jellyfin68K.bin' \
	  '  make clean   Remove build output' \
	  '' \
	  'Environment overrides:' \
	  '  RETRO68_TOOLCHAIN  Native Retro68 toolchain instead of Docker' \
	  '  RETRO68_IMAGE      Container image (default ghcr.io/autc04/retro68:latest)' \
	  '  BUILD_DIR          Output directory (default build)'

build:
	@./scripts/build.sh

clean:
	@rm -rf build
	@echo "Removed build/"
