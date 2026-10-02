# Makefile for softline - multiline readline replacement derived from linenoise
#
# Lifecycle spine: deps -> configure -> build -> test -> hardening -> package -> verify -> release

ROOT_DIR := $(shell pwd)
BUILD_DIR := $(ROOT_DIR)/build
DIST_DIR  := $(ROOT_DIR)/dist
CACHE_DIR := $(ROOT_DIR)/.cache

NINJA := $(shell command -v ninja 2>/dev/null || command -v ninja-build 2>/dev/null)
PRESET ?= debug

ifneq ($(strip $(THEME)),)
EXAMPLE_THEME_ENV := SOFTLINE_PROMPT_THEME="$(THEME)"
endif

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

.PHONY: deps
deps: ## Configure one dependency closure (DEPENDENCY=libmdf|lua PRESET=debug)
	@case "$(DEPENDENCY):$(PRESET)" in \
		libmdf:debug) cmake --preset debug ;; \
		lua:debug|lua:debug-lua) cmake --preset "$(PRESET)" && \
			sh ./scripts/build-local-lua.sh "$(BUILD_DIR)/$(PRESET)" ;; \
		*) echo "ERROR: use DEPENDENCY=libmdf PRESET=debug or DEPENDENCY=lua PRESET=debug|debug-lua" >&2; exit 2 ;; \
	esac

.PHONY: build
build: ## Build debug target
	@cmake --preset debug
	@cmake --build --preset debug

.PHONY: build-debug
build-debug: build ## Build debug target

.PHONY: run-simple
run-simple: build-debug ## Run C simple example (THEME=default|plain|...)
	@$(EXAMPLE_THEME_ENV) ./build/debug/examples/example_simple

.PHONY: run-chat
run-chat: build-debug ## Run C chat example (Gruvbox by default; THEME=... overrides)
	@SOFTLINE_PROMPT_THEME="$(if $(strip $(THEME)),$(THEME),gruvbox)" ./build/debug/examples/example_chat

.PHONY: run-chat-default
run-chat-default: build-debug ## Run C chat example with the default ANSI theme
	@SOFTLINE_PROMPT_THEME="default" ./build/debug/examples/example_chat

.PHONY: run-chat-riced
run-chat-riced: build-debug ## Run C chat example with the riced theme
	@SOFTLINE_PROMPT_THEME="riced" ./build/debug/examples/example_chat

.PHONY: run-chat-without-delay
run-chat-without-delay: build-debug ## Run default chat with no simulated producer delay
	@SOFTLINE_CHAT_CHAR_MS=0 SOFTLINE_PROMPT_THEME=default ./build/debug/examples/example_chat

.PHONY: run-chat-without-delay-riced
run-chat-without-delay-riced: build-debug ## Run riced chat with no simulated producer delay
	@SOFTLINE_CHAT_CHAR_MS=0 SOFTLINE_PROMPT_THEME=riced ./build/debug/examples/example_chat

.PHONY: run-chat-plain
run-chat-plain: build-debug ## Run C chat example with the uncoloured plain theme
	@SOFTLINE_PROMPT_THEME="plain" ./build/debug/examples/example_chat

.PHONY: run-chat-monogreen
run-chat-monogreen: build-debug ## Run C chat example with the monogreen theme
	@SOFTLINE_PROMPT_THEME="monogreen" ./build/debug/examples/example_chat

.PHONY: run-chat-monochrome
run-chat-monochrome: build-debug ## Run C chat example with the monochrome theme
	@SOFTLINE_PROMPT_THEME="monochrome" ./build/debug/examples/example_chat

.PHONY: run-chat-synthwave
run-chat-synthwave: build-debug ## Run C chat example with the synthwave theme
	@SOFTLINE_PROMPT_THEME="synthwave" ./build/debug/examples/example_chat

.PHONY: build-release
build-release: ## Build release target
	@cmake --preset x86_64-linux-gnu-release
	@cmake --build --preset x86_64-linux-gnu-release

.PHONY: test
test: ## Run debug tests with cached native terminal tools
	@sh ./scripts/terminal-test-env.sh $(MAKE) test-native

.PHONY: test-native
test-native: build-debug ## Run debug tests in the prepared test environment
	@cd $(BUILD_DIR)/debug && ctest --output-on-failure

.PHONY: deps-terminal-tests
deps-terminal-tests: ## Cache pinned native GTK/VTE/Xvfb test tools without host installation
	@sh ./scripts/terminal-test-env.sh

.PHONY: test-terminal
test-terminal: ## Run native debug tests with lifecycle-cached terminal test tools
	@$(MAKE) test

.PHONY: test-debug
test-debug: test ## Run debug tests

.PHONY: test-all
test-all: test test-terminal-cache test-runtime-config test-artifact-runtime test-lua-env test-lua-platform asan valgrind-portable fuzz-portable lua-test test-tool-discovery test-toolchain-contract test-darwin-linker-route test-lifecycle-surface test-lua-artifact-privacy test-public-header-docs ## Run all deterministic local tests

.PHONY: test-terminal-cache
test-terminal-cache: ## Verify offline reconstruction of cached terminal test tools
	@python3 tests/check_terminal_test_cache.py

.PHONY: test-lua-platform
test-lua-platform: ## Verify local Lua builds with Darwin platform settings
	@python3 tests/check_lua_platform.py

.PHONY: test-lua-env
test-lua-env: ## Verify Lua environments reject missing local interpreters
	@python3 tests/check_lua_env.py
	@python3 tests/check_lua_runtime_selection.py

.PHONY: test-artifact-runtime
test-artifact-runtime: ## Reject local runtime paths in release artifacts
	@python3 tests/check_artifact_portability.py
	@python3 tests/check_artifact_runtime.py

.PHONY: test-runtime-config
test-runtime-config: ## Verify missing-runtime and archive cache failures
	@python3 tests/check_runtime_config.py "$(ROOT_DIR)"

.PHONY: asan
asan: ## Run ASan+UBSan tests with cached native terminal tools
	@sh ./scripts/terminal-test-env.sh $(MAKE) asan-native

.PHONY: asan-native
asan-native: ## Run ASan+UBSan in the prepared test environment
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
			--trace-children=yes --log-file=memcheck-%p.log \
			./tests/test_examples \
			./examples/example_simple ./examples/example_chat
	@cd $(BUILD_DIR)/valgrind && \
		valgrind --leak-check=full --track-origins=yes --error-exitcode=1 \
			./tests/test_libmdf_stream

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
	@$(EXAMPLE_THEME_ENV) ./scripts/lua-debug.sh simple

.PHONY: lua-debug-chat
lua-debug-chat: ## Run examples/chat.lua against build/debug/libsoftline
	@$(EXAMPLE_THEME_ENV) ./scripts/lua-debug.sh chat

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
	@python3 tests/test_deps_target.py "$(ROOT_DIR)"
	@python3 tests/test_chat_targets.py "$(ROOT_DIR)"
	@python3 tests/check_source_smoke_workspace.py "$(ROOT_DIR)"

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
release: ## Clean final release build, including source reconstruction
	@$(MAKE) lifecycle-version-contract
	@$(MAKE) clean
	@SOFTLINE_REQUIRE_DARWIN=1 $(MAKE) release-pipeline
	@$(MAKE) package-source
	@$(MAKE) package-source-smoke
	@$(MAKE) package-checksums
	@$(MAKE) package-verify

.PHONY: release-pipeline
release-pipeline: ## Ordinary proof graph plus binary release matrix
	@$(MAKE) prerelease-checks
	@$(MAKE) release-matrix

.PHONY: prerelease-checks
prerelease-checks: format test-all test-release-version test-package-source-worktree test-clangd package-consumer-smoke

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
