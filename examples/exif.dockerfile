FROM conffuzz-dev:latest

RUN apt -y update
RUN apt install -y libpopt-dev subversion autoconf autopoint automake libtool nasm pkgconf
RUN apt -y build-dep libpcre3-dev

# Clone and build libexif
RUN git clone https://github.com/libexif/libexif.git
RUN cd libexif && autoreconf -i && ./configure CFLAGS="-g -fsanitize=address" && make && make install

# Clone and build exit
RUN git clone https://github.com/libexif/exif.git
RUN cd exif && autoreconf -i && ./configure CFLAGS="-g -fsanitize=address" && make && make install

# Clone and configure exif test suite
RUN git clone https://github.com/libexif/libexif-testsuite.git
RUN cd libexif-testsuite && ./build-config.sh && autoreconf -vis && ./configure

COPY examples/exif_sample.jpg /root/exif_sample.jpg

# Run test suite with 'make check'

WORKDIR /root
