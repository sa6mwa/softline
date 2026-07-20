# Makefile for softline - multiline readline replacement derived from linenoise
#
# Lifecycle spine: deps -> configure -> build -> test -> hardening -> package -> verify -> release

ROOT_DIR := $(shell pwd)
BUILD_DIR := $(ROOT_DIR)/build
DIST_DIR  := $(ROOT_DIR)/dist
CACHE_DIR := $(ROOT_DIR)/.cache

NINJA := $(shell command -v ninja 2>/dev/null || command -v ninja-build 2>/dev/null)

.PHONY: help
help: ## Show this help
	@echo "softline -- C89 multiline readline replacement derived from linenoise"
	@echo ""
	@echo "Lifecycle targets:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-30s\033[0m %s\n", $$1, $$2}'

.PHONY: format
format: ## Format source files with clang-format
	@command -v clang-format >/dev/null 2>&1 || { echo "ERROR: clang-format is required" >&2; exit 1; }
	@find include src tests examples lua \( -name '*.c' -o -name '*.h' \) \
		-exec clang-format -i -style=file --fallback-style=none {} +

.PHONY: deps-debug
deps-debug: ## Configure debug build dependencies
	@cmake --preset debug

.PHONY: deps-release
deps-release: ## Configure release build dependencies
	@cmake --preset x86_64-linux-gnu-release

.PHONY: deps-cross
deps-cross: ## Provision and inspect all pinned cross toolchains
	@./scripts/cpkt-toolchains.sh ensure all

.PHONY: build
build: ## Build debug target
	@cmake --preset debug
	@cmake --build --preset debug

.PHONY: build-debug
build-debug: build ## Build debug target

.PHONY: build-release
build-release: ## Build release target
	@cmake --preset x86_64-linux-gnu-release
	@cmake --build --preset x86_64-linux-gnu-release

.PHONY: test
test: build-debug ## Run debug tests
	@cd $(BUILD_DIR)/debug && ctest --output-on-failure

.PHONY: test-debug
test-debug: test ## Run debug tests

.PHONY: test-all
test-all: test asan valgrind-portable fuzz-portable lua-test ## Run all deterministic local tests

.PHONY: asan
asan: ## Run ASan+UBSan tests
	@cmake --preset asan && cmake --build --preset asan && \
		cd $(BUILD_DIR)/asan && ctest --output-on-failure

.PHONY: valgrind
valgrind: ## Run native Valgrind Memcheck tests
	@command -v valgrind >/dev/null 2>&1 || { echo "ERROR: valgrind is required for the native memory-check gate" >&2; exit 1; }
	@cmake --preset valgrind
	@cmake --build --preset valgrind
	@cd $(BUILD_DIR)/valgrind && \
		valgrind --leak-check=full --track-origins=yes --error-exitcode=1 ./tests/test_softline
	@cd $(BUILD_DIR)/valgrind && \
		valgrind --leak-check=full --track-origins=yes --error-exitcode=1 \
			--trace-children=yes ./tests/test_examples \
			./examples/example_simple ./examples/example_chat

.PHONY: valgrind-portable
valgrind-portable: ## Run native Valgrind on supported Linux hosts, skip where unsupported
	@case "$$(uname -s):$$(uname -m)" in \
		Linux:x86_64|Linux:amd64) $(MAKE) valgrind ;; \
		*) echo "SKIP: valgrind requires native x86_64 Linux" ;; \
	esac

.PHONY: fuzz-smoke
fuzz-smoke: ## Build and execute a bounded native AFL++ smoke run
	@timeout 600 ./scripts/cpkt-aflpp.sh ensure
	@cmake --preset fuzz
	@cmake --build --preset fuzz
	@timeout 15 "$${CPKT_AFL_SHOWMAP:-$$(./scripts/cpkt-aflpp.sh discover | sed -n 's/^afl_showmap=//p')}" -q -o /dev/null -- ./build/fuzz/softline_stdin_fuzz < fuzz/corpus/basic

.PHONY: fuzz-portable
fuzz-portable: ## Run native fuzz smoke on x86_64 hosts, skip where unsupported
	@case "$$(uname -s):$$(uname -m)" in \
		Linux:x86_64|Linux:amd64) $(MAKE) fuzz-smoke ;; \
		*) echo "SKIP: fuzz-smoke requires native x86_64 Linux" ;; \
	esac

.PHONY: fuzz
fuzz: fuzz-smoke ## Run the standard bounded native AFL++ fuzz gate

.PHONY: fuzz-long
fuzz-long: ## Run a longer opt-in native AFL++ fuzz session
	@cmake --preset fuzz
	@cmake --build --preset fuzz
	@"$$(./scripts/cpkt-aflpp.sh discover | sed -n 's/^afl_fuzz=//p')" -i fuzz/corpus -o build/fuzz-findings -- ./build/fuzz/softline_stdin_fuzz

.PHONY: lua-rock
lua-rock: ## Build and install Lua facade into repo-local LuaRocks tree
	@./scripts/lua-test.sh

.PHONY: lua-test
lua-test: ## Run Lua facade smoke tests
	@./scripts/lua-test.sh

.PHONY: lua-test-lib64
lua-test-lib64: ## Run Lua facade smoke tests with a lib64 SDK install
	@SOFTLINE_LUA_INSTALL_LIBDIR=lib64 ./scripts/lua-test.sh

.PHONY: lua-env
lua-env: ## Print shell exports for repo-local Lua facade
	@./scripts/lua-env.sh

.PHONY: lua-debug-test
lua-debug-test: ## Run Lua facade and examples against build/debug/libsoftline
	@./scripts/lua-debug.sh test

.PHONY: lua-debug-env
lua-debug-env: ## Print shell exports for Lua facade against build/debug/libsoftline
	@./scripts/lua-debug.sh env

.PHONY: lua-debug-simple
lua-debug-simple: ## Run examples/simple.lua against build/debug/libsoftline
	@./scripts/lua-debug.sh simple

.PHONY: lua-debug-chat
lua-debug-chat: ## Run examples/chat.lua against build/debug/libsoftline
	@./scripts/lua-debug.sh chat

.PHONY: package
package: ## Build release packages
	@./scripts/package.sh

.PHONY: package-checksums
package-checksums: ## Generate checksums for release artifacts
	@./scripts/package-checksums.sh

.PHONY: package-verify
package-verify: package-checksums ## Verify release packages
	@./scripts/package-verify.sh

.PHONY: package-source
package-source: ## Create source archive
	@./scripts/package-source.sh

.PHONY: package-source-smoke
package-source-smoke: package-source ## Verify source archive builds
	@./scripts/package-source-smoke.sh

.PHONY: package-consumer-smoke
package-consumer-smoke: ## Verify installed CMake package from an external consumer
	@./scripts/package-consumer-smoke.sh

.PHONY: test-tool-discovery
test-tool-discovery: ## Verify cross-target tool discovery
	@./scripts/test_discover_target_tools.sh

.PHONY: test-darwin-linker-route
test-darwin-linker-route: ## Verify osxcross Darwin links use the target linker
	@./scripts/test_darwin_linker_route.sh

.PHONY: test-release-version
test-release-version: ## Verify release version source precedence
	@./scripts/test_release_version.sh

.PHONY: test-package-source-worktree
test-package-source-worktree: ## Verify source packaging from a linked Git worktree
	@./scripts/test_package_source_worktree.sh

.PHONY: test-toolchain-contract
test-toolchain-contract: ## Verify toolchain provisioning and environment contracts
	@./scripts/test_toolchain_contract.sh

.PHONY: test-lifecycle-surface
test-lifecycle-surface: ## Verify standard lifecycle command and preset surfaces
	@./scripts/test_lifecycle_surface.sh

.PHONY: test-clangd
test-clangd: deps-debug ## Verify clangd project configuration and semantic parsing
	@./scripts/test_clangd.sh

.PHONY: test-public-header-docs
test-public-header-docs: ## Verify public headers have API documentation comments
	@./scripts/test_public_header_docs.sh

.PHONY: release-lua-artifacts
release-lua-artifacts: ## Build Lua source package, release rockspec, and source rock
	@./scripts/release_lua_artifacts.sh

.PHONY: validate-luarocks
validate-luarocks: ## Verify LuaRocks release artifacts
	@./scripts/validate_luarocks.sh

.PHONY: test-lua-artifact-privacy
test-lua-artifact-privacy: ## Verify Lua release artifacts fail on local path leaks
	@./scripts/test_lua_artifact_privacy.sh

.PHONY: verify-release-archives
verify-release-archives: package-verify ## Verify all release archives

.PHONY: verify-release-privacy
verify-release-privacy: ## Scan release artifacts for local paths
	@./scripts/verify-release-privacy.sh

.PHONY: release-matrix
release-matrix: ## Build, package, checksum, and verify all release targets
	@./scripts/run_linux_release_matrix.sh

.PHONY: finalize-slice
finalize-slice: format test ## Pre-commit gate: format + debug tests

.PHONY: prerelease
prerelease: ## Deterministic pre-release verification
	@$(MAKE) release-pipeline

.PHONY: prerelease-hardening
prerelease-hardening: ## Expensive hardening gate
	@$(MAKE) release-pipeline

.PHONY: release
release: ## Clean release build
	@$(MAKE) lifecycle-version-contract
	@$(MAKE) clean
	@SOFTLINE_REQUIRE_DARWIN=1 $(MAKE) release-pipeline

.PHONY: release-pipeline
release-pipeline: ## Shared clean release proof graph
	@$(MAKE) prerelease-checks
	@$(MAKE) release-matrix

.PHONY: prerelease-checks
prerelease-checks: format test-all test-tool-discovery test-toolchain-contract test-darwin-linker-route test-release-version test-package-source-worktree test-lifecycle-surface test-lua-artifact-privacy test-clangd test-public-header-docs package-consumer-smoke package-source package-source-smoke

.PHONY: lifecycle-version-contract
lifecycle-version-contract: ## Verify exact lightweight-tag release version behavior
	@./scripts/lifecycle-version-contract.sh

.PHONY: print-release-version
print-release-version: ## Print the current release version
	@./scripts/release_version.sh

.PHONY: clean
clean: ## Remove all generated build and dist artifacts
	@./scripts/clean.sh

.PHONY: clean-dist
clean-dist: ## Remove dist/ artifacts only
	@rm -rf $(DIST_DIR)
