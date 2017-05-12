omr_dir := ./omr
jitbuilder_dir := $(omr_dir)/jitbuilder/release
jitbuilder_includes := -I$(jitbuilder_dir)/include -I$(jitbuilder_dir)/include/compiler
jitbuilder_lib := $(jitbuilder_dir)/libjitbuilder.a

bf: $(jitbuilder_lib)

LDFLAGS := -ldl
CXXFLAGS := $(jitbuilder_includes) -gdwarf -fno-rtti -std=c++11 -Wall -Wextra

$(jitbuilder_lib): $(omr_dir)
	(cd $(omr_dir)/; ./configure)
	(cd $(omr_dir)/jitbuilder; make)

tidy:
	clang-tidy bf.cpp -- $(CXXFLAGS)

clean:
	$(RM) bf

.PHONY:tidy clean
