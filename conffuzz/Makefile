all: instrumentation.so conffuzz
dbg: instrumentation.so-dbg conffuzz-dbg

mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
mkfile_dir := $(dir $(mkfile_path))
PIN_HOME=$(mkfile_dir)/../pintools
PIN_EXAMPLES_HOME=$(PIN_HOME)/source/tools/SimpleExamples/

.PHONY: instrumentation.so
instrumentation.so:
	cp $(mkfile_dir)/instrumentation.cpp $(PIN_EXAMPLES_HOME)
	cp -r $(mkfile_dir)/include $(PIN_EXAMPLES_HOME)
	sed -i -e "s/opcodemix/instrumentation/g" $(PIN_EXAMPLES_HOME)/makefile.rules
	cd $(PIN_EXAMPLES_HOME) && make obj-intel64/instrumentation.so
	cp $(PIN_EXAMPLES_HOME)/obj-intel64/instrumentation.so $(mkfile_dir)/

# unfortunately we cannot compile the pin tool with asan
instrumentation.so-dbg:
	cp $(mkfile_dir)/instrumentation.cpp $(PIN_EXAMPLES_HOME)
	cp -r $(mkfile_dir)/include $(PIN_EXAMPLES_HOME)
	sed -i -e "s/opcodemix/instrumentation/g" $(PIN_EXAMPLES_HOME)/makefile.rules
	cd $(PIN_EXAMPLES_HOME) && make obj-intel64/instrumentation.so DEBUG=1
	cp $(PIN_EXAMPLES_HOME)/obj-intel64/instrumentation.so $(mkfile_dir)/

.PHONY: conffuzz
conffuzz:
	g++ -Wall -std=c++17 -o conffuzz conffuzz.cpp

conffuzz-dbg:
	g++ -Wall -std=c++17 -g -O0 -fsanitize=address -o conffuzz conffuzz.cpp

clean:
	rm -rf $(mkfile_dir)/instrumentation.so $(mkfile_dir)/conffuzz
