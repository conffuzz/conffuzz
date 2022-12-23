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

# Install redis-benchmark
RUN apt install -y redis-tools

# Build Redis
RUN apt install -y build-essential cmake m4 automake peg libtool autoconf python3
RUN wget https://download.redis.io/releases/redis-6.2.6.tar.gz
RUN tar -xf redis-6.2.6.tar.gz

# disable SIGSEGV (& co.) handlers
RUN sed -i "s/sigaction(SIG/\/\/sigaction(SIG/" /root/redis-6.2.6/src/server.c

RUN cd redis-6.2.6 && make -j CFLAGS="-O0 -g -fsanitize=address" LDFLAGS="-fsanitize=address"

# Build RediSearch
RUN git clone https://github.com/RediSearch/RediSearch.git
RUN cd RediSearch && git checkout f3577f7e
RUN cd RediSearch && git submodule update --init
RUN cd RediSearch && make setup || :
RUN cd RediSearch && \
    make build DEBUG=1 && \
    sed -i "s/CMAKE_C_FLAGS_DEBUG:STRING=-g/CMAKE_C_FLAGS_DEBUG:STRING=-g -O0/g" \
        bin/linux-x64-debug/search/CMakeCache.txt && \
    make DEBUG=1 CMAKE_ARGS="--log-level=VERBOSE"

# Build RedisBloom
RUN git clone https://github.com/RedisBloom/RedisBloom.git
RUN cd RedisBloom && git checkout 8b6ee3ba0e4814f7a1a6bf630f3838cff625f0ab
RUN cd RedisBloom && sed -i "s/static //g" src/rebloom.c
RUN cd RedisBloom && git submodule update --init && make DEBUG=1 -j

# Somehow this seems necessary
RUN sed -i "s/bind 127.0.0.1/# bind 127.0.0.1/" /root/redis-6.2.6/redis.conf

# Disable snapshots
RUN echo 'save ""' >> /root/redis-6.2.6/redis.conf

# generate API description
# note: we use conffuzz from the docker-cache for this step to avoid having to
# redo it every single time we do a change to conffuzz/
COPY ./resources/docker-cache/ /root/
RUN make genapi-redis
# we get a fresh copy of conffuzz when we actually mount the container
RUN rm -rf /root/conffuzz
