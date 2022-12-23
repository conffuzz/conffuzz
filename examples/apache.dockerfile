FROM debian:11.5

# note: we are *not* basing on the conffuzz base repository; this is because
# for some obscure reasons, ConfFuzz with Apache on top of Ubuntu 22.04. I
# have idea why. This is likely a similar problem to what we are encountering
# with Okular.
#
# Heureux là-bas sur l’onde, et bercé du hasard,
# Un pêcheur indolent qui flotte et chante, ignore
# Quelle foudre s’amasse au centre de César.

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

RUN apt -y update && apt install -y unzip curl libpcre3-dev && apt -y build-dep apache2

# Build apache
WORKDIR /root
RUN wget https://archive.apache.org/dist/httpd/httpd-2.4.52.tar.gz
RUN tar -xf httpd-2.4.52.tar.gz
RUN rm httpd-2.4.52.tar.gz
RUN cd httpd-2.4.52 && \
    CFLAGS="-O0 -g -fsanitize=address" ./configure --disable-luajit && \
    make -j && make install
ENV LIBAPACHE_APXS=/root/httpd-2.4.52/support/apxs
RUN chmod u+x ${LIBAPACHE_APXS}

# Build libmarkdown
WORKDIR /root
RUN wget http://www.pell.portland.or.us/~orc/Code/discount/discount-2.2.7.tar.bz2
RUN tar -xf discount-2.2.7.tar.bz2
RUN rm discount-2.2.7.tar.bz2
RUN cd discount-2.2.7 && ./configure.sh --shared --prefix=/usr
RUN sed -i "s/O3/O0 -g3/g" discount-2.2.7/Makefile
RUN cd discount-2.2.7 && make && make install

# Build mod_markdown
WORKDIR /root
RUN wget https://github.com/hamano/apache-mod-markdown/archive/1bf4fb4df6029e8fdfc5ce46f14e99d951230450.zip
RUN unzip 1bf4fb4df6029e8fdfc5ce46f14e99d951230450.zip
RUN rm 1bf4fb4df6029e8fdfc5ce46f14e99d951230450.zip
RUN mv apache-mod-markdown-1bf4fb4df6029e8fdfc5ce46f14e99d951230450 apache-mod-markdown
RUN cd apache-mod-markdown && \
    autoreconf -f -i && \
    CFLAGS="-O0 -g -fsanitize=address" ./configure --with-apxs=${LIBAPACHE_APXS} && \
    make -j && make install

# install mod_markdown, enable it in Apache
RUN echo "LoadModule markdown_module /usr/local/apache2/modules/mod_markdown.so" >> \
            /usr/local/apache2/conf/httpd.conf
RUN echo "<Directory /usr/local/apache2/htdocs/>\nAddHandler markdown .md\nDirectoryIndex index.md\n</Directory>" >> \
            /usr/local/apache2/conf/httpd.conf
RUN echo "# Hello markdown!" >> /usr/local/apache2/htdocs/index.md

# disable memory pools that conflict w/ ASan
RUN echo "MaxMemFree 1" >> /usr/local/apache2/conf/httpd.conf

# disable multiprocess
RUN echo "<IfModule mpm_event_module>" >> /usr/local/apache2/conf/httpd.conf
RUN echo "    StartServers             1" >> /usr/local/apache2/conf/httpd.conf
RUN echo "    MinSpareThreads          1" >> /usr/local/apache2/conf/httpd.conf
RUN echo "    MaxSpareThreads          1" >> /usr/local/apache2/conf/httpd.conf
RUN echo "    ThreadsPerChild          1" >> /usr/local/apache2/conf/httpd.conf
RUN echo "    MaxRequestWorkers        1" >> /usr/local/apache2/conf/httpd.conf
RUN echo "    MaxConnectionsPerChild   0" >> /usr/local/apache2/conf/httpd.conf
RUN echo "    StartServers             1" >> /usr/local/apache2/conf/httpd.conf
RUN echo "</IfModule>" >> /usr/local/apache2/conf/httpd.conf

# log to stderr
RUN sed -i "s/logs\/error_log/\/dev\/stderr/g" /usr/local/apache2/conf/httpd.conf

WORKDIR /root

# Test that the markdown module works after starting the server:
# # curl http://172.17.0.2/
# <!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN"
#           "http://www.w3.org/TR/html4/loose.dtd">
# <html>
# <head>
# <meta http-equiv="Content-Type" content="text/html; charset=utf-8">
# <title></title>
# </head>
# <body>
# <a name="Hello-markdown-21-"></a>
# <h1>Hello markdown!</h1>
# </body>
# </html>

# generate API description
# note: we use conffuzz from the docker-cache for this step to avoid having to
# redo it every single time we do a change to conffuzz/
COPY ./resources/docker-cache/ /root/
RUN make genapi-apache
# we get a fresh copy of conffuzz when we actually mount the container
RUN rm -rf /root/conffuzz
