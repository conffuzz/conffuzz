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
                   build-essential libpcre2-dev libbz2-dev libssl-dev libcunit1 \
                   libcunit1-doc libcunit1-dev python3-sphinx cmake gettext autopoint

RUN wget https://github.com/rpm-software-management/popt/archive/refs/tags/popt-1.19-release.tar.gz
RUN tar -xf popt-1.19-release.tar.gz && rm popt-1.19-release.tar.gz
RUN cd popt-popt-1.19-release && ./autogen.sh
RUN cd popt-popt-1.19-release && ./configure CFLAGS="-g -O0"
RUN cd popt-popt-1.19-release && make
RUN cd popt-popt-1.19-release && make install

RUN wget https://download.samba.org/pub/rsync/src/rsync-3.2.7.tar.gz
RUN tar -xf rsync-3.2.7.tar.gz && rm rsync-3.2.7.tar.gz
RUN cd rsync-3.2.7 && ./configure CFLAGS="-g -O0 -fsanitize=address" LD_FLAGS="-fsanitize=address" --disable-xxhash --disable-zstd --disable-lz4
RUN cd rsync-3.2.7 && make
RUN cd rsync-3.2.7 && make install

# start with /root/rsync-3.2.7/rsync --daemon -g /root/README.md /root/NDSS
# to get relatively good coverage of popt

# generate API description
# note: we use conffuzz from the docker-cache for this step to avoid having to
# redo it every single time we do a change to conffuzz/
COPY ./resources/docker-cache/ /root/
RUN make genapi-rsync
# we get a fresh copy of conffuzz when we actually mount the container
RUN rm -rf /root/conffuzz
