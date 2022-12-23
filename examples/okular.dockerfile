FROM debian:11.5

# note: we are *not* basing on the conffuzz base repository; this is because
# for some obscure reasons, ConfFuzz doesn't work with Okular and very recent
# builds of Qt. We don't have time to investigate, so the easiest is to simply
# use an older version of Debian/Ubuntu as base image.

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
RUN apt install -y cmake pkg-config libfreetype-dev libboost-dev \
                   libfontconfig1-dev libjpeg-dev qtbase5-dev \
                   libopenjp2-7-dev

# Build libmarkdown
WORKDIR /root
RUN wget http://www.pell.portland.or.us/~orc/Code/discount/discount-2.2.7.tar.bz2
RUN tar -xf discount-2.2.7.tar.bz2
RUN rm discount-2.2.7.tar.bz2
RUN cd discount-2.2.7 && ./configure.sh --shared --prefix=/usr
RUN sed -i "s/O3/O0 -g3/g" discount-2.2.7/Makefile
RUN cd discount-2.2.7 && make && make install

# Build libpoppler-qt5
RUN git clone git://git.freedesktop.org/git/poppler/test
RUN git clone https://anongit.freedesktop.org/git/poppler/poppler.git
RUN cd poppler && \
    git checkout poppler-22.04.0 && \
    mkdir build && \
    cd build && \
    cmake -DCMAKE_BUILD_TYPE=debugfull -DBUILD_GTK_TESTS=OFF .. && \
    make && make install

# Build okular
RUN apt install -y extra-cmake-modules libqt5svg5-dev qml \
                   qtdeclarative5-dev libkf5activities-dev \
                   libkf5archive-dev libkf5bookmarks-dev \
                   libkf5completion-dev libkf5config-dev \
                   libkf5configwidgets-dev libkf5coreaddons-dev \
                   libkf5crash-dev libkf5doctools-dev \
                   libkf5i18n-dev libkf5iconthemes-dev \
                   libkf5kexiv2-dev libkf5khtml-dev \
                   libkf5kio-dev libkf5kjs-dev \
                   libkf5parts-dev libkf5pty-dev \
                   libkf5purpose-dev libkf5textwidgets-dev \
                   libkf5threadweaver-dev libkf5wallet-dev
RUN apt install -y libphonon4qt5-dev \
                   libphonon4qt5experimental-dev

RUN git clone https://invent.kde.org/graphics/okular.git
RUN apt install -y gettext
RUN cd okular && \
    git checkout v21.12.2 && \
    mkdir -p build/install build/make
RUN cd okular/build && \
    cmake -E env CXXFLAGS="-g -O0 -fsanitize=address" cmake .. && \
    make -j && \
    make install

COPY examples/sample.pdf /root/foo.pdf

RUN echo "# Markdown file" > /root/foo.md

# generate API description
# note: we use conffuzz from the docker-cache for this step to avoid having to
# redo it every single time we do a change to conffuzz/
COPY ./resources/docker-cache/ /root/
RUN make genapi-okular
# we get a fresh copy of conffuzz when we actually mount the container
RUN rm -rf /root/conffuzz
