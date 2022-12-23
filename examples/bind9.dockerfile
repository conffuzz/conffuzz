FROM debian:11.5

WORKDIR /root

ENV DEBIAN_FRONTEND=noninteractive
RUN apt update && apt install -y wget vim build-essential \
    llvm-11 binutils file git tree psmisc python3-pip moreutils

RUN pip install angr

# necessary for apt build-dep
RUN cp /etc/apt/sources.list /tmp/apt.tmp
RUN sed -i "s/deb /deb-src /g" /tmp/apt.tmp
RUN cat /tmp/apt.tmp >> /etc/apt/sources.list

ENV PATH "$PATH:/usr/local/bin"
ENV LD_LIBRARY_PATH "$LD_LIBRARY_PATH:/usr/local/lib"

RUN apt update

ARG DEBIAN_FRONTEND=noninteractive

RUN apt -y update
RUN apt install -y unzip curl wrk autoconf libtool libdeflate-dev pkg-config libuv1-dev \
                   build-essential libpcre2-dev libbz2-dev libssl-dev libcunit1 libcunit1-doc libcunit1-dev python3-sphinx cmake

# Install Nghttp2
RUN wget https://github.com/nghttp2/nghttp2/releases/download/v1.48.0/nghttp2-1.48.0.tar.gz
RUN tar xf nghttp2-1.48.0.tar.gz
RUN cd nghttp2-1.48.0/ && ./configure --enable-lib-only && mkdir build && cd build && cmake .. && cmake --build . && cmake --install .

# Install libxml2
RUN wget https://download.gnome.org/sources/libxml2/2.10/libxml2-2.10.3.tar.xz
RUN tar xf libxml2-2.10.3.tar.xz
RUN cd libxml2-2.10.3 && CFLAGS="-O0 -g" ./configure && make CFLAGS="-O0 -g" && make install

# Install bind9
RUN wget https://downloads.isc.org/isc/bind9/9.18.8/bind-9.18.8.tar.xz
RUN tar xf bind-9.18.8.tar.xz
RUN cd bind-9.18.8/ && CFLAGS="-O0 -g -fsanitize=address" LDFLAGS="-fsanitize=address" ./configure --enable-threads --disable-linux-caps --with-libxml2 && make -j

# Copy the configuration
COPY examples/named.conf /usr/local/etc/
RUN mkdir -p /var/cache/bind
RUN echo "export LD_LIBRARY_PATH='/root/bind-9.18.8/lib/isc/.libs:/root/bind-9.18.8/lib/dns/.libs:/root/bind-9.18.8/lib/ns/.libs:/root/bind-9.18.8/lib/isccc/.libs:/root/bind-9.18.8/lib/isccfg/.libs:/root/bind-9.18.8/lib/bind9/.libs:/usr/local/lib:D_LIBRARY_PATH'" >> /root/.bashrc

# generate API description
# note: we use conffuzz from the docker-cache for this step to avoid having to
# redo it every single time we do a change to conffuzz/
COPY ./resources/docker-cache/ /root/
RUN make genapi-bind9
# we get a fresh copy of conffuzz when we actually mount the container
RUN rm -rf /root/conffuzz
