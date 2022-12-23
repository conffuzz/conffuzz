FROM conffuzz-dev:latest

WORKDIR /root

# Build nghttp2 (HTTP/2 library)
RUN apt -y update
RUN apt -y install libpython3-dev
RUN apt -y build-dep nghttp2
RUN wget https://github.com/nghttp2/nghttp2/releases/download/v1.47.0/nghttp2-1.47.0.tar.gz
RUN tar -xf nghttp2-1.47.0.tar.gz
RUN rm nghttp2-1.47.0.tar.gz

RUN cd nghttp2-1.47.0 && \
    ./configure --enable-debug --disable-static && make && make install

# Build openssl
RUN wget http://www.openssl.org/source/openssl-1.1.1g.tar.gz
RUN tar -zxf openssl-1.1.1g.tar.gz
RUN cd openssl-1.1.1g && CFLAGS="-fsanitize=address -g -O0" ./config && make && make install

# Build curl
RUN apt install -y libssl-dev
RUN wget https://github.com/curl/curl/releases/download/curl-7_82_0/curl-7.82.0.tar.gz
RUN tar -xf curl-7.82.0.tar.gz
RUN rm curl-7.82.0.tar.gz

RUN cd curl-7.82.0 && \
    CFLAGS="-fsanitize=address" \
    ./configure --disable-static \
                --with-openssl \
                --enable-debug \
                --disable-optimize \
                --with-nghttp2
RUN cd curl-7.82.0 && make -j && make install
ENV LD_LIBRARY_PATH="/lib:/usr/lib:/usr/local/lib"

WORKDIR /root
