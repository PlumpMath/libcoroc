.PHONY: all library app clean format

all: library app

FORMATER := clang-format-3.5

enable_timer ?= 0
enable_debug ?= 1
enable_splitstack ?= 0
enable_deadlock_detect ?= 1
enable_futex ?= 1
enable_lockfree_runq ?= 0

ifeq ($(shell uname), Darwin)
    use_clang ?= 1
endif

ARGS := enable_timer=${enable_timer} \
	enable_debug=${enable_debug} \
	enable_splitstack=${enable_splitstack} \
	enable_deadlock_detect=${enable_deadlock_detect} \
	enable_futex=${enable_futex} \
	use_clang=${use_clang} \
	enable_lockfree_runq=${enable_lockfree_runq}

library:
	cd src && make install $(ARGS)

app:
	cd examples/C && make install $(ARGS)
	cd examples/CoroC && make install $(ARGS)

format:
	@find . -name "*.[c|h]" -print | xargs ${FORMATER} -i -style=Google

clean:
	cd src && make clean
	cd examples/C && make clean
	cd examples/CoroC && make clean
	rm -rf bin lib
