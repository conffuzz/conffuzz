FROM conffuzz-dev:latest

WORKDIR /root

RUN apt -y update && apt -y install autoconf automake libtool yacc bison \
                            flex apparmor systemctl clang openssh-server cmake \
                            pkg-config golang

# Clone & build openssl
# NOTE: ended up not using it because alternative SSL libs have less symbols
# RUN wget http://www.openssl.org/source/openssl-1.1.1g.tar.gz
# RUN tar -zxf openssl-1.1.1g.tar.gz
# RUN cd openssl-1.1.1g && CFLAGS="-fsanitize=address -g -O0" ./config && make -j && make install

# Clone & build boringssl
# NOTE: ended up not using it because OpenSSH doesn't compile with it
# RUN git clone https://boringssl.googlesource.com/boringssl
# RUN cd boringssl && git checkout 054a5d36bb4df09f4ecf62f6ddeb2439aa76d4ba && mkdir build
# RUN cd boringssl/build && cmake -DCMAKE_POSITION_INDEPENDENT_CODE=on -DBUILD_SHARED_LIBS=1 ..
# RUN cd boringssl/build && make -j && make install
# RUN cp /root/boringssl/install/lib/libcrypto.so /usr/local/lib/
# RUN cp -R /root/boringssl/install/include/openssl /usr/local/include

# Clone & build libressl
RUN wget https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/libressl-3.6.0.tar.gz
RUN tar -xf libressl-3.6.0.tar.gz && rm libressl-3.6.0.tar.gz
RUN cd libressl-3.6.0 && CFLAGS="-O0 -g -fsanitize=address" LDFLAGS="-fsanitize=address" ./configure
RUN cd libressl-3.6.0 && make -j && make install

# Clone & build openssh
RUN wget https://ftp.hostserver.de/pub/OpenBSD/OpenSSH/portable/openssh-9.1p1.tar.gz
RUN tar -xf openssh-9.1p1.tar.gz && rm openssh-9.1p1.tar.gz
# Prevent ssh from closing all file descriptors, as this also
# closes the instrumentation's pipes...
# This is the sort of things that causes instrumentation bug messages
RUN sed -i "s/closefrom(STDERR_FILENO + 1);/\/\/closefrom(STDERR_FILENO + 1);/g" \
           openssh-9.1p1/ssh.c
RUN cd openssh-9.1p1 && CFLAGS="-O0 -g -fsanitize=address" LDFLAGS="-fsanitize=address" \
                        ./configure --with-sandbox=no --disable-strip
RUN cd openssh-9.1p1 && make -j
RUN cd openssh-9.1p1 && ASAN_OPTIONS=detect_leaks=0 make install

# we need to generate a key...
RUN ssh-keygen -t rsa -b 4096 -f /root/.ssh/id_rsa -N ""
RUN cp .ssh/id_rsa.pub .ssh/authorized_keys

WORKDIR /root

# start with /usr/local/bin/ssh -o StrictHostKeyChecking=no localhost ls /root

# libcrypto is quite huge, if you want a symbol to experiment with,
# take OPENSSL_init_crypto, as it is the first one called.

# generate API description
# note: we use conffuzz from the docker-cache for this step to avoid having to
# redo it every single time we do a change to conffuzz/
COPY ./resources/docker-cache/ /root/
RUN make genapi-ssh
# we get a fresh copy of conffuzz when we actually mount the container
RUN rm -rf /root/conffuzz
