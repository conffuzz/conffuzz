FROM conffuzz-dev:latest

WORKDIR /root

RUN apt -y update
RUN apt -y build-dep inkscape

RUN rm -rf /usr/lib/x86_64-linux-gnu/libpoppler.so*

# Build libtiff
RUN wget https://download.osgeo.org/libtiff/tiff-4.3.0.tar.gz
RUN tar -xf tiff-4.3.0.tar.gz
RUN rm tiff-4.3.0.tar.gz
RUN cd tiff-4.3.0 && CFLAGS="-g" ./configure && make && make install

ENV LD_LIBRARY_PATH="/lib:/usr/lib:/usr/local/lib"

# Build libpng
RUN wget https://github.com/glennrp/libpng/archive/refs/tags/v1.6.37.tar.gz
RUN tar -xf v1.6.37.tar.gz
RUN rm v1.6.37.tar.gz
RUN cd libpng-1.6.37 && CFLAGS="-g" ./configure && make && make install

ENV LD_LIBRARY_PATH="/lib:/usr/lib:/usr/local/lib"

# Build libpoppler
RUN git clone git://git.freedesktop.org/git/poppler/test
RUN wget https://poppler.freedesktop.org/poppler-22.02.0.tar.xz
RUN tar -xf poppler-22.02.0.tar.xz && rm poppler-22.02.0.tar.xz
RUN cd poppler-22.02.0 && \
    mkdir build && \
    cd build && \
    cmake -DCMAKE_BUILD_TYPE=debugfull -DBUILD_GTK_TESTS=OFF .. && \
    make && make install

ENV LD_LIBRARY_PATH="/lib:/usr/lib:/usr/local/lib"

# Build inkscape
RUN wget https://inkscape.org/gallery/item/31668/inkscape-1.1.2.tar.xz
RUN tar -xf inkscape-1.1.2.tar.xz
RUN rm inkscape-1.1.2.tar.xz
RUN mv inkscape-1.1.2_2022-02-04_0a00cf5339 inkscape-INKSCAPE_1_1_2

# disable SIGSEGV (& co.) handlers
RUN sed -i "s/segv_handler = signal (SIGSEGV,/\/\/ segv_handler = signal (SIGSEGV,/" /root/inkscape-INKSCAPE_1_1_2/src/inkscape.cpp
RUN sed -i "s/abrt_handler = signal (SIGABRT,/\/\/ abrt_handler = signal (SIGABRT,/" /root/inkscape-INKSCAPE_1_1_2/src/inkscape.cpp
RUN sed -i "s/fpe_handler  = signal (SIGFPE,/\/\/ fpe_handler  = signal (SIGFPE,/" /root/inkscape-INKSCAPE_1_1_2/src/inkscape.cpp
RUN sed -i "s/ill_handler  = signal (SIGILL,/\/\/ ill_handler  = signal (SIGILL,/" /root/inkscape-INKSCAPE_1_1_2/src/inkscape.cpp

# necessary to fix a build system bug with parallel builds
# https://gitlab.com/inkscape/inkscape/-/merge_requests/4114
RUN sed -i "s/get_inkscape_languages/add_dependencies(default_templates pofiles)\nget_inkscape_languages/g" inkscape-INKSCAPE_1_1_2/share/templates/CMakeLists.txt
RUN cd inkscape-INKSCAPE_1_1_2 && \
    mkdir build && \
    cd build && \
    CFLAGS="-fsanitize=address -O0 -g" LDFLAGS="-fsanitize=address" cmake ..
RUN cd inkscape-INKSCAPE_1_1_2/build && make -j6
RUN cd inkscape-INKSCAPE_1_1_2/build && make install

# disable leaks, they are problematic with inkscape for some reason
ENV ASAN_OPTIONS=detect_leaks=0

COPY examples/sample.pdf /root/foo.pdf

WORKDIR /root

# generate API description
# note: we use conffuzz from the docker-cache for this step to avoid having to
# redo it every single time we do a change to conffuzz/
COPY ./resources/docker-cache/ /root/
RUN make genapi-inkscape
# we get a fresh copy of conffuzz when we actually mount the container
RUN rm -rf /root/conffuzz
