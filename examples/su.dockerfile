FROM conffuzz-dev:latest

WORKDIR /root

RUN apt -y update && apt -y install autoconf automake libtool yacc bison flex apparmor systemctl clang

# Build libaudit
RUN wget https://people.redhat.com/sgrubb/audit/audit-3.0.9.tar.gz
RUN tar -xf audit-3.0.9.tar.gz
RUN rm audit-3.0.9.tar.gz
RUN apt -y update && apt -y install swig
RUN cd audit-3.0.9 && CFLAGS="-O0 -g" ./configure --disable-zos-remote && make && make install

# Build su
RUN wget https://github.com/shadow-maint/shadow/releases/download/4.13/shadow-4.13.tar.gz
RUN tar -xf shadow-4.13.tar.gz
RUN rm shadow-4.13.tar.gz
RUN cd shadow-4.13 && CFLAGS="-O0 -g -fsanitize=address" LDFLAGS="-fsanitize=address" \
    ./configure --with-audit && make && make install

WORKDIR /root

# run with /root/shadow-4.13/src/su -c 'echo "hello"' root

# generate API description
# note: we use conffuzz from the docker-cache for this step to avoid having to
# redo it every single time we do a change to conffuzz/
COPY ./resources/docker-cache/ /root/
RUN make genapi-su
# we get a fresh copy of conffuzz when we actually mount the container
RUN rm -rf /root/conffuzz
