FROM conffuzz-dev:latest

RUN apt -y update
RUN apt install -y unzip curl wrk autoconf build-essential libtool libdeflate-dev \
                   pkg-config libuv1-dev build-essential libpcre2-dev libbz2-dev \
                   libssl-dev libcunit1 libcunit1-doc libcunit1-dev python3-sphinx cmake

# Build libexpat2
RUN wget https://github.com/libexpat/libexpat/releases/download/R_2_5_0/expat-2.5.0.tar.gz
RUN tar xzf expat-2.5.0.tar.gz
RUN cd expat-2.5.0 && CFLAGS="-O0 -g" ./configure && make -j && make install

# Install libxml2
RUN wget https://download.gnome.org/sources/libxml2/2.10/libxml2-2.10.3.tar.xz
RUN tar xf libxml2-2.10.3.tar.xz
RUN cd libxml2-2.10.3 && CFLAGS="-O0 -g" ./configure && make CFLAGS="-O0 -g" && make install

# Build Squid
RUN wget http://www.squid-cache.org/Versions/v5/squid-5.7.tar.gz
RUN tar xzf squid-5.7.tar.gz
RUN cd squid-5.7 && CFLAGS="-O0 -g -fsanitize=address" LDFLAGS="-fsanitize=address" ./configure --disable-ipv6 --enable-esi --disable-optimizations
RUN cd squid-5.7 && find . -type f -exec sed -i 's/-Werror/-Wno-error/g' {} +
RUN cd squid-5.7 && sed -i "s/setEffectiveUser();//g" src/main.cc
RUN cd squid-5.7 && make -j && make install

# Add the default squid configuration
RUN mkdir -p /usr/local/squid/etc/
COPY examples/squid.conf /usr/local/squid/etc/squid.conf
COPY examples/mime.conf /usr/local/squid/etc/mime.conf

RUN apt update -y && apt install -y nginx

RUN echo "cache_effective_user root" >> /usr/local/squid/etc/squid.conf
RUN echo "cache_effective_group root" >> /usr/local/squid/etc/squid.conf
RUN echo "esi_parser libxml2" >> /usr/local/squid/etc/squid.conf
RUN echo "http_port 100 accel vport=80" >> /usr/local/squid/etc/squid.conf
RUN echo "cache_peer 127.0.0.1 parent 80 0 no-query originserver" >> /usr/local/squid/etc/squid.conf

RUN sed -i "21a add_header Surrogate-Control 'content=\"ESI/1.0\"';" /etc/nginx/nginx.conf

RUN sed -i "14a <esi:assign name=\"test_string\" value=\"This is test\"/>" /var/www/html/index.nginx-debian.html
RUN sed -i "14a <esi:vars> \$(test_string) </esi:vars>" /var/www/html/index.nginx-debian.html

# generate API description
# note: we use conffuzz from the docker-cache for this step to avoid having to
# redo it every single time we do a change to conffuzz/
COPY ./resources/docker-cache/ /root/
RUN make genapi-squid
# we get a fresh copy of conffuzz when we actually mount the container
RUN rm -rf /root/conffuzz

# service nginx start
# run with ASAN_OPTIONS=new_delete_type_mismatch=0 /usr/local/squid/sbin/squid -N -f /usr/local/squid/etc/squid.conf
# test with curl localhost:100
