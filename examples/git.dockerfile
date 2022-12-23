FROM conffuzz-curl:latest

WORKDIR /root

RUN apt -y update
RUN apt -y install dh-autoreconf libcurl4-gnutls-dev libexpat1-dev \
  gettext libz-dev libssl-dev

# Build libpcre2
RUN apt -y build-dep libpcre3-dev
RUN wget https://github.com/PhilipHazel/pcre2/releases/download/pcre2-10.39/pcre2-10.39.tar.gz
RUN tar -xf pcre2-10.39.tar.gz
RUN cd pcre2-10.39 && CFLAGS="-g -O0" ./configure && make install

# Build zlib
RUN wget https://zlib.net/fossils/zlib-1.2.12.tar.gz
RUN tar -xf zlib-1.2.12.tar.gz
RUN rm -rf zlib-1.2.12.tar.gz
RUN cd zlib-1.2.12 && CFLAGS="-O0 -g" ./configure && \
    make && make install

# Build git
RUN wget https://mirrors.edge.kernel.org/pub/software/scm/git/git-2.35.1.tar.gz
RUN tar -xf git-2.35.1.tar.gz
RUN rm git-2.35.1.tar.gz

RUN cd git-2.35.1 && make configure
RUN cd git-2.35.1 && CFLAGS="-O0 -g -fsanitize=address" ./configure --with-curl --with-libpcre2
RUN cd git-2.35.1 && make -j && make install

ENV LD_LIBRARY_PATH="/lib:/usr/lib:/usr/local/lib"
# disable leaks, they are problematic with git for some reason
ENV ASAN_OPTIONS=detect_leaks=0

# example repo
RUN git clone https://github.com/project-flexos/sqlite-splitsrc.git
RUN mv sqlite-splitsrc repo-example

WORKDIR /root

# generate API description
# note: we use conffuzz from the docker-cache for this step to avoid having to
# redo it every single time we do a change to conffuzz/
COPY ./resources/docker-cache/ /root/
RUN make genapi-git
# we get a fresh copy of conffuzz when we actually mount the container
RUN rm -rf /root/conffuzz
