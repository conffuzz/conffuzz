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

# Install libxml2
RUN wget https://download.gnome.org/sources/libxml2/2.10/libxml2-2.10.3.tar.xz
RUN tar xf libxml2-2.10.3.tar.xz
RUN cd libxml2-2.10.3 && CFLAGS="-O0 -g" ./configure && make CFLAGS="-O0 -g" && make install
RUN sed -i "s/testapi_LDFLAGS = $/testapi_LDFLAGS = -fsanitize=address/g" /root/libxml2-2.10.3/Makefile
RUN cd libxml2-2.10.3 && make CFLAGS="-O0 -g" check
