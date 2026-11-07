# FVD native developer shortcuts. Native configuration uses CMake directly.
# Linux: make build / make test / make package (Windows: build-fvd.cmd).
.PHONY: all build package test python-test solo relay run clean checkfast checkpy help
all: build
build:
	bash build-fvd.sh build
package:
	bash build-fvd.sh package
test:
	bash build-fvd.sh test
python-test:
	bash build-fvd.sh python-test
solo:
	bash build-fvd.sh solo
relay:
	bash build-fvd.sh relay
run: solo
clean:
	cmake -E rm -rf build
checkfast:
	python3 -m buildsystem.codecompliance --fast
checkpy:
	uv run --no-sync python3 -m buildsystem.codecompliance --ruff --ty
help:
	@echo 'FVD native: build, package, test, python-test, solo, relay, clean, checkfast, checkpy'
	@echo 'For options: bash build-fvd.sh --help'
