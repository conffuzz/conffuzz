FROM conffuzz-dev:latest

# Build file
WORKDIR /root
RUN wget http://deb.debian.org/debian/pool/main/f/file/file_5.41.orig.tar.gz
RUN tar -xf file_5.41.orig.tar.gz
RUN rm file_5.41.orig.tar.gz
RUN apt -y update
RUN apt -y build-dep file
RUN cd file-5.41 && \
    CFLAGS="-O0 -g -fsanitize=address" ./configure && \
    make && make install
RUN cp /usr/local/lib/libmagic.so* /usr/lib/x86_64-linux-gnu/

WORKDIR /root
