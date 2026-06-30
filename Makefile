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
	@find include src tests examples lua -name '*.c' -o -name '*.h' | sort | \
		xargs clang-format -i -style=file --fallback-style=none 2>/dev/null || true

.PHONY: deps-debug
deps-debug: ## Configure debug build dependencies
	@cmake --preset debug

.PHONY: deps-release
deps-release: ## Configure release build dependencies
	@cmake --preset x86_64-linux-gnu-release

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
test-all: test asan ## Run all local tests

.PHONY: asan
asan: ## Run ASan+UBSan tests
	@cmake --preset asan && cmake --build --preset asan && \
		cd $(BUILD_DIR)/asan && ctest --output-on-failure

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
package-source-smoke: ## Verify source archive builds
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
prerelease: format test asan test-tool-discovery test-darwin-linker-route test-release-version test-lifecycle-surface test-clangd test-public-header-docs package-consumer-smoke lua-test ## Deterministic pre-release verification

.PHONY: prerelease-hardening
prerelease-hardening: prerelease release-matrix ## Expensive hardening gate

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
