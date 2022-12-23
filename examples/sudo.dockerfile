FROM conffuzz-dev:latest

WORKDIR /root

RUN apt -y update && apt -y install autoconf automake libtool yacc bison flex apparmor systemctl clang

# building with clang might not be necessary, but hey, it works like that.

# Build libapparmor
RUN wget https://gitlab.com/apparmor/apparmor/-/archive/v3.1.1/apparmor-v3.1.1.tar.gz
RUN tar -xf apparmor-v3.1.1.tar.gz
RUN rm apparmor-v3.1.1.tar.gz
# the sed is to be able to compiled with clang
RUN cd apparmor-v3.1.1/libraries/libapparmor/ && \
    ./autogen.sh && \
    CC=clang CFLAGS="-O0 -g -fsanitize=address" LDFLAGS="-fsanitize=address" ./configure && \
    sed -i "s/-flto-partition=none//g" /root/apparmor-v3.1.1/libraries/libapparmor/src/Makefile
RUN cd apparmor-v3.1.1/libraries/libapparmor/ && make install

# Build sudo
COPY examples/sudo.patch /root/sudo.patch
RUN wget https://www.sudo.ws/dist/sudo-1.9.12.tar.gz
RUN tar -xf sudo-1.9.12.tar.gz
RUN rm sudo-1.9.12.tar.gz
RUN cd sudo-1.9.12 && patch -p1 < /root/sudo.patch && \
    CC=clang CFLAGS="-O0 -g" ./configure --with-apparmor --enable-sanitizer && \
    make && make install

# script to enable apparmor
RUN echo "#!/bin/bash" > /root/start-apparmor.sh
RUN echo "systemctl enable apparmor" > /root/start-apparmor.sh
RUN echo "systemctl start apparmor" > /root/start-apparmor.sh
RUN chmod u+x /root/start-apparmor.sh

# enable apparmor profile enforcement in sudo
# make sure that the timeout is very high
RUN sed -i "s/root ALL=(ALL:ALL) ALL/root ALL=(ALL:ALL) APPARMOR_PROFILE=unconfined ALL\nDefaults:USER timestamp_timeout=-1/g" /etc/sudoers

# and that's again for the timeout, just to make sure...
RUN echo "Defaults        env_reset,timestamp_timeout=-1" >> /etc/sudoers

# we need to have root password enabled...
RUN echo "root:x" | chpasswd

WORKDIR /root

# start with echo "x" | LD_PRELOAD=/lib/x86_64-linux-gnu/libcrypt.so.1.1.0 sudo -S man
# - LD_PRELOADing libcrypt is necessary to circumvent a compiler bug
# - x is the root password

# generate API description
# note: we use conffuzz from the docker-cache for this step to avoid having to
# redo it every single time we do a change to conffuzz/
COPY ./resources/docker-cache/ /root/
RUN make genapi-sudo
# we get a fresh copy of conffuzz when we actually mount the container
RUN rm -rf /root/conffuzz
