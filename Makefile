SHELL := /usr/bin/env bash
.DEFAULT_GOAL := help

RETRO68_IMAGE ?= ghcr.io/autc04/retro68:latest

.PHONY: help setup build run relay shell doctor images reset-state clean distclean

help:
	@printf '%s\n' \
	  'Retro68 68K Mac development environment' \
	  '' \
	  '  make setup       Check prerequisites, pull the toolchain, build the client' \
	  '  make build       Cross-compile app/ into build/Jellyfin68K.dsk' \
	  '  make run         Build, then boot System 7 in Basilisk II' \
	  '  make relay       Forward 10.0.2.2:8096 to the Jellyfin server' \
	  '  make shell       Open a shell with the Retro68 compiler in PATH' \
	  '  make doctor      Check Docker, Basilisk II, images, and build output' \
	  '  make images      Install ROM/system images (see make images ARGS=...)' \
	  '  make reset-state Re-copy the pristine system image (discard Mac changes)' \
	  '  make clean       Remove build output' \
	  '' \
	  'Examples:' \
	  '  make images ARGS="--rom ~/Mac.ROM --system ~/System7.img"' \
	  '  make images ARGS="--fetch-system --rom ~/Mac.ROM"' \
	  '' \
	  'Environment overrides:' \
	  '  RETRO68_TOOLCHAIN  Native Retro68 toolchain instead of Docker' \
	  '  RETRO68_IMAGE      Container image (default ghcr.io/autc04/retro68:latest)' \
	  '  MAC_SCREEN         Emulator size (default 512x342, a Macintosh Classic)' \
	  '  MAC_COLOR_DEPTH    Emulator bit depth (default 8)' \
	  '  MAC_RAM_MB         Emulated RAM in MB (default 64)' \
	  '  MAC_MODEL_ID       Basilisk II model ID (default 14, Quadra 950)' \
	  '  MAC_CPU            Basilisk II CPU ID (default 4, 68040)' \
	  '  MAC_FPU=0          Emulate a 68LC040 with no FPU (default 1)' \
	  '  MAC_GUI=1          Show the Basilisk II settings window before booting' \
	  '  MAC_SOUND=1        Enable Sound Manager audio (default)'

setup:
	@./scripts/setup.sh

build:
	@./scripts/build.sh

run: build
	@./scripts/run.sh

relay:
	@python3 ./scripts/jellyfin-relay.py

shell:
	@./scripts/shell.sh

doctor:
	@./scripts/doctor.sh

images:
	@./scripts/setup-images.sh $(ARGS)

reset-state:
	@rm -rf .state
	@echo "Removed .state; the pristine system image will be copied on next run."

clean:
	@rm -rf build
	@echo "Removed build/"

distclean: clean reset-state
