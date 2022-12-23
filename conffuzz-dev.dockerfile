FROM ubuntu:22.04

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
