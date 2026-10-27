.PHONY: help configure build run clean check-build-dir eval-bp-basic eval-bp test-prim regression-tage

.DEFAULT_GOAL := help

BUILD_DIR := build
CMAKE := cmake
RUN_TARGETS := $(filter-out run,$(MAKECMDGOALS))
VALID_RUN_TARGETS := eval-bp-basic eval-bp test-prim regression-tage

help:
	@echo "usage:"
	@echo "  make run eval-bp-basic"
	@echo "  make run eval-bp"
	@echo "  make run test-prim"
	@echo "  make run regression-tage"
	@echo "  make build"
	@echo "  make clean"

check-build-dir:
	@if [ -e "$(BUILD_DIR)" ] && [ ! -d "$(BUILD_DIR)" ]; then \
		echo "error: $(BUILD_DIR) exists but is not a directory"; \
		exit 1; \
	fi

$(BUILD_DIR)/CMakeCache.txt: CMakeLists.txt | check-build-dir
	$(CMAKE) -S . -B $(BUILD_DIR)

configure: $(BUILD_DIR)/CMakeCache.txt

build: configure
	$(CMAKE) --build $(BUILD_DIR)

run: configure
	@if [ -z "$(RUN_TARGETS)" ]; then \
		echo "usage: make run <target>"; \
		echo "targets: $(VALID_RUN_TARGETS)"; \
		exit 2; \
	fi
	@for target in $(RUN_TARGETS); do \
		case " $(VALID_RUN_TARGETS) " in \
			*" $$target "*) $(CMAKE) --build "$(BUILD_DIR)" --target "$$target" || exit $$? ;; \
			*) echo "unknown run target: $$target"; echo "targets: $(VALID_RUN_TARGETS)"; exit 2 ;; \
		esac; \
	done

clean:
	@if [ -d "$(BUILD_DIR)" ]; then \
		rm -rf "$(BUILD_DIR)"; \
	else \
		echo "No $(BUILD_DIR) directory to clean"; \
	fi

ifeq ($(firstword $(MAKECMDGOALS)),run)
eval-bp-basic eval-bp test-prim regression-tage:
	@:
else
eval-bp-basic eval-bp test-prim regression-tage: configure
	$(CMAKE) --build $(BUILD_DIR) --target $@
endif
