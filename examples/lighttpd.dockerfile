FROM conffuzz-dev:latest

RUN apt -y update
RUN apt install -y unzip curl wrk autoconf libtool libdeflate-dev \
                   build-essential libpcre2-dev libbz2-dev

RUN wget https://download.lighttpd.net/lighttpd/releases-1.4.x/lighttpd-1.4.67.tar.gz
RUN tar -xf lighttpd-1.4.67.tar.gz

# RUN cd lighttpd-1.4.67 && ./autogen.sh
RUN cd lighttpd-1.4.67 && ./configure -C \
        --with-libdeflate --with-bzip2 --with-zlib \
	CFLAGS="-O0 -g -fsanitize=address" \
	LDFLAGS="-lpthread -fsanitize=address"
RUN cd lighttpd-1.4.67 && make install

RUN echo "server.modules = (" >> /root/lighttpd.conf
RUN echo "	\"mod_deflate\"," >> /root/lighttpd.conf
RUN echo ")\n" >> /root/lighttpd.conf
RUN echo "server.document-root        = \"/root\"" >> /root/lighttpd.conf
RUN echo "server.upload-dirs          = ( \"/root\" )" >> /root/lighttpd.conf
RUN echo "server.errorlog             = \"error.log\"" >> /root/lighttpd.conf
RUN echo "server.pid-file             = \"lighttpd.pid\"" >> /root/lighttpd.conf
RUN echo "server.port                 = 80" >> /root/lighttpd.conf
RUN echo "index-file.names            = ( \"index.html\" )" >> /root/lighttpd.conf
RUN echo "static-file.exclude-extensions = ( \".php\", \".pl\", \".fcgi\" )" >> /root/lighttpd.conf

# mod_deflate options
RUN echo "deflate.compression-level = 9" >> /root/lighttpd.conf

RUN echo "ConfFuzz rocks!" >> /root/index.html

WORKDIR /root

# start lighttpd with ./lighttpd-1.4.67/src/lighttpd -f lighttpd.conf -D

# generate API description
# note: we use conffuzz from the docker-cache for this step to avoid having to
# redo it every single time we do a change to conffuzz/
COPY ./resources/docker-cache/ /root/
RUN make genapi-lighttpd
# we get a fresh copy of conffuzz when we actually mount the container
RUN rm -rf /root/conffuzz
