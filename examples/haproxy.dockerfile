FROM debian:11.5

WORKDIR /root

ENV DEBIAN_FRONTEND=noninteractive
RUN apt update && apt install -y wget vim build-essential \
    llvm-11 binutils file git tree psmisc python3-pip moreutils

RUN pip install angr

# necessary for apt build-dep

# older version of Debian/Ubuntu
RUN cp /etc/apt/sources.list /tmp/apt.tmp
RUN sed -i "s/deb /deb-src /g" /tmp/apt.tmp
RUN cat /tmp/apt.tmp >> /etc/apt/sources.list

# modern version
# RUN cp /etc/apt/sources.list.d/debian.sources /etc/apt/sources.list.d/debian-src.sources
# RUN sed -i "s/Types: deb/Types: deb-src/g" /etc/apt/sources.list.d/debian-src.sources

ENV PATH "$PATH:/usr/local/bin"
ENV LD_LIBRARY_PATH "$LD_LIBRARY_PATH:/usr/local/lib"

RUN apt update

ARG DEBIAN_FRONTEND=noninteractive


RUN apt -y update
RUN apt install -y unzip curl wrk autoconf build-essential libmount-dev python3-jinja2 pkg-config meson gperf libcap-dev apache2

# Build libsystemd
RUN wget https://github.com/systemd/systemd/archive/refs/tags/v251.tar.gz
RUN tar xf v251.tar.gz
RUN cd systemd-251 && ./configure && cd build && ninja

# Build libszl
RUN wget https://github.com/wtarreau/libslz/archive/refs/tags/v1.2.0.tar.gz
RUN tar xf v1.2.0.tar.gz
RUN cd libslz-1.2.0/ && make OPT_CFLAGS=-O0 && make install

# Build haproxy
RUN wget https://www.haproxy.org/download/2.6/src/snapshot/haproxy-ss-LATEST.tar.gz
RUN tar xf haproxy-ss-LATEST.tar.gz
RUN cd haproxy-ss-20221026/ && make TARGET=linux-glibc USE_THREAD=1 USE_SYSTEMD=1 V=1 ADDINC="-I/root/systemd-251/src/ -O0 -g -fsanitize=address" ADDLIB="-L/root/systemd-251/build -fsanitize=address" USE_SLZ=1 -j
RUN mkdir /etc/haproxy/
COPY examples/haproxy.cfg /etc/haproxy/

# Disable apache compression, 
RUN a2dismod deflate -f
#RUN service apache2 start

# generate API description
# note: we use conffuzz from the docker-cache for this step to avoid having to
# redo it every single time we do a change to conffuzz/
COPY ./resources/docker-cache/ /root/
RUN make genapi-haproxy
# we get a fresh copy of conffuzz when we actually mount the container
RUN rm -rf /root/conffuzz
