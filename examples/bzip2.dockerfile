FROM conffuzz-dev:latest

# Build bzip2 and libbzip2
WORKDIR /root
RUN wget https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
RUN tar -xf bzip2-1.0.8.tar.gz
RUN rm bzip2-1.0.8.tar.gz

# first build without ASan for the lib
RUN sed -i "s/O2/O0/" bzip2-1.0.8/Makefile-libbz2_so
RUN cd bzip2-1.0.8 && make -f Makefile-libbz2_so
RUN rm -rf /lib/x86_64-linux-gnu/libbz2.so*
RUN cp bzip2-1.0.8/libbz2.so* /lib/x86_64-linux-gnu/
RUN ln -s /lib/x86_64-linux-gnu/libbz2.so.1.0 /lib/x86_64-linux-gnu/libbz2.so.1

# second build for the app
RUN cd bzip2-1.0.8 && make -f Makefile-libbz2_so clean
RUN sed -i "s/O0/O0 -fsanitize=address/" bzip2-1.0.8/Makefile-libbz2_so
# disable custom SIGSEGV handler... hides bugs without fixing them...
RUN sed -i "s/signal (SIGSEGV/\/\/ signal (SIGSEGV/" bzip2-1.0.8/bzip2.c
RUN sed -i "s/signal (SIGBUS/\/\/ signal (SIGBUS/" bzip2-1.0.8/bzip2.c
RUN cd bzip2-1.0.8 && make -f Makefile-libbz2_so

RUN echo "text" > /tmp/example.txt
RUN ./bzip2-1.0.8/bzip2-shared /tmp/example.txt
RUN mv /tmp/example.txt.bz2 example.bz2

WORKDIR /root

# generate API description
# note: we use conffuzz from the docker-cache for this step to avoid having to
# redo it every single time we do a change to conffuzz/
COPY ./resources/docker-cache/ /root/
RUN make genapi-bzip2
# we get a fresh copy of conffuzz when we actually mount the container
RUN rm -rf /root/conffuzz
