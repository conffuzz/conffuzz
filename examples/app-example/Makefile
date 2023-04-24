# Change this if you installed pin somewhere else

mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
mkfile_dir := $(dir $(mkfile_path))
PIN_HOME=$(mkfile_dir)/../../pintools

# set this before running the false positive test, system dependent
LIBRARY_DIR :=

# dependency for all tests
libexample := $(mkfile_dir)/../lib-example/libexample.so

DEBUG_OPT := -d

all: appexample workloadscripttest reentrancetest argptrtest callbacktest \
     falsepositivestest deepstructtest nonreproducibletest nonasantest run

$(libexample):
	make -C $(dir $@)

appexample: $(libexample)
	gcc -g -L$(mkfile_dir)/../lib-example/ -I$(mkfile_dir)/../lib-example/include/ \
		-O0 -fsanitize=address -Wall -o appexample main.c -lexample

reentrancetest: $(libexample)
	gcc -g -L$(mkfile_dir)/../lib-example/ -I$(mkfile_dir)/../lib-example/include/ \
		-O0 -fsanitize=address -Wall -o reentrancetest reentrance.c -lexample

argptrtest: $(libexample)
	gcc -g -L$(mkfile_dir)/../lib-example/ -I$(mkfile_dir)/../lib-example/include/ \
		-O0 -fsanitize=address -Wall -o argptrtest argptr.c -lexample

callbacktest: $(libexample)
	gcc -g -L$(mkfile_dir)/../lib-example/ -I$(mkfile_dir)/../lib-example/include/ \
		-O0 -fsanitize=address -Wall -o callbacktest callback.c -lexample

workloadscripttest: $(libexample)
	gcc -g -L$(mkfile_dir)/../lib-example/ -I$(mkfile_dir)/../lib-example/include/ \
		-O0 -fsanitize=address -Wall -o workloadscripttest workloadscript.c -lexample

falsepositivestest: $(libexample)
	gcc -g -L$(mkfile_dir)/../lib-example/ -I$(mkfile_dir)/../lib-example/include/ \
		-O0 -fsanitize=address -Wall -o falsepositivestest falsepositives.c -lexample

deepstructtest: $(libexample)
	gcc -g -L$(mkfile_dir)/../lib-example/ -I$(mkfile_dir)/../lib-example/include/ \
		-O0 -fsanitize=address -Wall -o deepstructtest deepstruct.c -lexample

nonreproducibletest: $(libexample)
	gcc -g -L$(mkfile_dir)/../lib-example/ -I$(mkfile_dir)/../lib-example/include/ \
		-O0 -fsanitize=address -Wall -o nonreproducibletest non-reproducible.c -lexample

nonasantest: $(libexample)
	gcc -g -L$(mkfile_dir)/../lib-example/ -I$(mkfile_dir)/../lib-example/include/ \
		-O0 -fsanitize=address -Wall -o nonasantest non-asan.c -lexample


run: appexample
	ASAN_OPTIONS=detect_leaks=0 LD_LIBRARY_PATH=$(mkfile_dir)/../lib-example/:$LD_LIBRARY_PATH ./appexample

run-instrumented: appexample
	LD_LIBRARY_PATH=$(mkfile_dir)/../lib-example/:$LD_LIBRARY_PATH \
		$(mkfile_dir)/../../conffuzz/conffuzz \
		$(DEBUG_OPT) $(mkfile_dir)/../lib-example/libexample.so ./appexample \
		arg1 arg2 arg3

run-instrumented-safebox: appexample
	LD_LIBRARY_PATH=$(mkfile_dir)/../lib-example/:$LD_LIBRARY_PATH \
		$(mkfile_dir)/../../conffuzz/conffuzz -R \
		$(DEBUG_OPT) $(mkfile_dir)/../lib-example/libexample.so ./appexample \
		arg1 arg2 arg3

run-reentrance-instrumented: reentrancetest
	LD_LIBRARY_PATH=$(mkfile_dir)/../lib-example/:$LD_LIBRARY_PATH \
		$(mkfile_dir)/../../conffuzz/conffuzz \
		$(DEBUG_OPT) $(mkfile_dir)/../lib-example/libexample.so ./reentrancetest

run-argptr-instrumented: argptrtest
	LD_LIBRARY_PATH=$(mkfile_dir)/../lib-example/:$LD_LIBRARY_PATH \
		$(mkfile_dir)/../../conffuzz/conffuzz \
		$(DEBUG_OPT) $(mkfile_dir)/../lib-example/libexample.so ./argptrtest

run-callback-instrumented: callbacktest
	LD_LIBRARY_PATH=$(mkfile_dir)/../lib-example/:$LD_LIBRARY_PATH \
		$(mkfile_dir)/../../conffuzz/conffuzz \
		$(DEBUG_OPT) $(mkfile_dir)/../lib-example/libexample.so ./callbacktest

run-callback-instrumented-safebox: callbacktest
	LD_LIBRARY_PATH=$(mkfile_dir)/../lib-example/:$LD_LIBRARY_PATH \
		$(mkfile_dir)/../../conffuzz/conffuzz -R \
		$(DEBUG_OPT) $(mkfile_dir)/../lib-example/libexample.so ./callbacktest

run-workloadscript-instrumented: workloadscripttest
	LD_LIBRARY_PATH=$(mkfile_dir)/../lib-example/:$LD_LIBRARY_PATH \
		$(mkfile_dir)/../../conffuzz/conffuzz -t $(mkfile_dir)/workloadscripttest.sh \
		$(DEBUG_OPT) $(mkfile_dir)/../lib-example/libexample.so ./workloadscripttest

run-falsepositives-instrumented: falsepositivestest
	LD_LIBRARY_PATH=$(mkfile_dir)/../lib-example/:$LD_LIBRARY_PATH \
		$(mkfile_dir)/../../conffuzz/conffuzz \
		$(DEBUG_OPT) $(mkfile_dir)/../lib-example/libexample.so ./falsepositivestest

run-deepstructtest-instrumented: deepstructtest
	LD_LIBRARY_PATH=$(mkfile_dir)/../lib-example/:$LD_LIBRARY_PATH \
		$(mkfile_dir)/../../conffuzz/conffuzz \
		$(DEBUG_OPT) $(mkfile_dir)/../lib-example/libexample.so ./deepstructtest

run-nonreproducibletest-instrumented: nonreproducibletest
	LD_LIBRARY_PATH=$(mkfile_dir)/../lib-example/:$LD_LIBRARY_PATH \
		$(mkfile_dir)/../../conffuzz/conffuzz \
		$(DEBUG_OPT) $(mkfile_dir)/../lib-example/libexample.so ./nonreproducibletest

run-nonasantest-instrumented: nonasantest
	LD_LIBRARY_PATH=$(mkfile_dir)/../lib-example/:$LD_LIBRARY_PATH \
		$(mkfile_dir)/../../conffuzz/conffuzz \
		$(DEBUG_OPT) $(mkfile_dir)/../lib-example/libexample.so ./nonasantest

clean:
	rm -rf appexample workloadscripttest reentrancetest argptrtest callbacktest \
               falsepositivestest deepstructtest nonreproducibletest nonasantest *.out
