FROM conffuzz-dev:latest

RUN apt -y update
RUN apt -y build-dep graphicsmagick && apt install -y libltdl-dev

# Build libghostscript
RUN wget https://github.com/ArtifexSoftware/ghostpdl-downloads/releases/download/gs9550/ghostscript-9.55.0.tar.gz
RUN tar -xf ghostscript-9.55.0.tar.gz
# hack, but needed; gs's build scripts are buggy
RUN sed -i "s/O2/O0 -g3/g" ghostscript-9.55.0/configure
RUN cd ghostscript-9.55.0 && ./configure --enable-hidden-visibility && make debug && make install
RUN cd ghostscript-9.55.0 && make clean && make sodebug && make sodebuginstall

# Build libtiff
RUN wget https://download.osgeo.org/libtiff/tiff-4.3.0.tar.gz
RUN tar -xf tiff-4.3.0.tar.gz
RUN rm tiff-4.3.0.tar.gz
RUN cd tiff-4.3.0 && CFLAGS="-g" ./configure && make && make install

# Build libpng
RUN wget https://github.com/glennrp/libpng/archive/refs/tags/v1.6.37.tar.gz
RUN tar -xf v1.6.37.tar.gz
RUN rm v1.6.37.tar.gz
RUN cd libpng-1.6.37 && CFLAGS="-g" ./configure && make && make install

ENV LD_LIBRARY_PATH="/lib:/usr/lib:/usr/local/lib"

# Build imagemagick
RUN wget https://github.com/ImageMagick/ImageMagick/archive/refs/tags/7.1.0-26.tar.gz
RUN tar -xf 7.1.0-26.tar.gz
RUN cd ImageMagick-7.1.0-26 && \
    CFLAGS="-O0 -g -fsanitize=address" ./configure --with-gslib=yes \
                                     --with-tiff \
                                     --with-gs-font-dir=/usr/share/ghostscript/fonts/ \
                                     --enable-delegate-build \
                                     --with-modules
RUN cd ImageMagick-7.1.0-26 && make install
RUN ldconfig /usr/local/lib

RUN echo "%!" >> /root/foo.ps
RUN echo "(Hello world) print" >> /root/foo.ps

# generate API description
# note: we use conffuzz from the docker-cache for this step to avoid having to
# redo it every single time we do a change to conffuzz/
COPY ./resources/docker-cache/ /root/
RUN make genapi-magick
# we get a fresh copy of conffuzz when we actually mount the container
RUN rm -rf /root/conffuzz

COPY examples/sample.pdf /root/foo.pdf
