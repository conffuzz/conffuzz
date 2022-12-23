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


# Build LibSASL
RUN wget https://github.com/cyrusimap/cyrus-sasl/releases/download/cyrus-sasl-2.1.27/cyrus-sasl-2.1.27.tar.gz
RUN tar xf cyrus-sasl-2.1.27.tar.gz
RUN cd cyrus-sasl-2.1.27 && CFLAGS="-O0 -g" ./configure && make CFLAGS="-O0 -g" && make install

# Build Memcached
RUN apt install -y build-essential cmake m4 automake peg libtool autoconf python3 gcc make libevent-dev libc6-dev --no-install-recommends
RUN wget http://www.memcached.org/files/memcached-1.6.17.tar.gz
RUN tar -xf memcached-1.6.17.tar.gz
RUN cd memcached-1.6.17/ && LIBS="-lpthread" CFLAGS="-O0 -g -fsanitize=address" LDFLAGS="-fsanitize=address" ./configure --prefix=/usr/local/memcached --enable-sasl-pwdb --enable-sasl 
RUN cd memcached-1.6.17/ && make -j CFLAGS="-O0 -g -fsanitize=address" LDFLAGS="-fsanitize=address" 

# Configure memcached to run with sasl
RUN mkdir -p ~/working/sasl
RUN echo "export SASL_CONF_PATH=$HOME/working/sasl" >> ~/.bashrc
RUN echo "export MEMCACHED_SASL_PWDB=$HOME/working/sasl/my.sasldb" >> ~/.bashrc
RUN echo "root@2a46ea8ec91e:1234" > ~/working/sasl/my.sasldb
RUN echo "mech_list: plain cram-md5" > ~/working/sasl/memcached.conf
RUN echo "log_level: 5" >> ~/working/sasl/memcached.conf
RUN echo "sasldb_path: ~/working/sasl/my.sasldb" >> ~/working/sasl/memcached.conf

# For the client
RUN pip install python-binary-memcached

# generate API description
# note: we use conffuzz from the docker-cache for this step to avoid having to
# redo it every single time we do a change to conffuzz/
COPY ./resources/docker-cache/ /root/
RUN make genapi-memcached
# we get a fresh copy of conffuzz when we actually mount the container
RUN rm -rf /root/conffuzz
