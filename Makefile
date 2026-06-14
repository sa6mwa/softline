# Makefile for softline - readline replacement extending linenoise
#
# Lifecycle spine: deps -> configure -> build -> test -> hardening -> package -> verify -> release

ROOT_DIR := $(shell pwd)
BUILD_DIR := $(ROOT_DIR)/build
DIST_DIR  := $(ROOT_DIR)/dist
CACHE_DIR := $(ROOT_DIR)/.cache

NINJA := $(shell command -v ninja 2>/dev/null || command -v ninja-build 2>/dev/null)

.PHONY: help
help: ## Show this help
	@echo "softline -- C89 readline replacement extending linenoise"
	@echo ""
	@echo "Lifecycle targets:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-30s\033[0m %s\n", $$1, $$2}'

.PHONY: format
format: ## Format source files with clang-format
	@find include src tests examples -name '*.c' -o -name '*.h' | sort | \
		xargs clang-format -i -style=file --fallback-style=none 2>/dev/null || true

.PHONY: deps-debug
deps-debug: ## Configure debug build dependencies
	@cmake --preset debug

.PHONY: deps-release
deps-release: ## Configure release build dependencies
	@cmake --preset x86_64-linux-gnu-release

.PHONY: build
build: ## Build debug target
	@cmake --build --preset debug

.PHONY: build-debug
build-debug: build ## Build debug target

.PHONY: build-release
build-release: ## Build release target
	@cmake --build --preset x86_64-linux-gnu-release

.PHONY: test
test: ## Run debug tests
	@cd $(BUILD_DIR)/debug && ctest --output-on-failure

.PHONY: test-debug
test-debug: test ## Run debug tests

.PHONY: test-all
test-all: test asan ## Run all local tests

.PHONY: asan
asan: ## Run ASan+UBSan tests
	@cmake --preset asan && cmake --build --preset asan && \
		cd $(BUILD_DIR)/asan && ctest --output-on-failure

.PHONY: package
package: ## Build release packages
	@./scripts/package.sh

.PHONY: package-checksums
package-checksums: ## Generate checksums for release artifacts
	@./scripts/package-checksums.sh

.PHONY: package-verify
package-verify: ## Verify release packages
	@./scripts/package-verify.sh

.PHONY: package-source
package-source: ## Create source archive
	@./scripts/package-source.sh

.PHONY: package-source-smoke
package-source-smoke: ## Verify source archive builds
	@./scripts/package-source-smoke.sh

.PHONY: verify-release-archives
verify-release-archives: package-verify ## Verify all release archives

.PHONY: verify-release-privacy
verify-release-privacy: ## Scan release artifacts for local paths
	@./scripts/verify-release-privacy.sh

.PHONY: release-matrix
release-matrix: ## Build and package all release targets
	@./scripts/run_linux_release_matrix.sh

.PHONY: finalize-slice
finalize-slice: format test ## Pre-commit gate: format + debug tests

.PHONY: prerelease
prerelease: format test asan ## Deterministic pre-release verification

.PHONY: prerelease-hardening
prerelease-hardening: prerelease release-matrix package-verify ## Expensive hardening gate

.PHONY: release
release: ## Clean release build
	@./scripts/release.sh

.PHONY: print-release-version
print-release-version: ## Print the current release version
	@./scripts/release_version.sh

.PHONY: clean
clean: ## Remove all generated build and dist artifacts
	@./scripts/clean.sh

.PHONY: clean-dist
clean-dist: ## Remove dist/ artifacts only
	@rm -rf $(DIST_DIR)