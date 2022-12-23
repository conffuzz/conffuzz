FROM conffuzz-dev:latest

RUN apt -y update
RUN apt install -y unzip curl wrk

# Clone and build libpcre2
RUN apt -y build-dep libpcre3-dev
RUN wget https://github.com/PhilipHazel/pcre2/releases/download/pcre2-10.39/pcre2-10.39.tar.gz
RUN tar -xf pcre2-10.39.tar.gz

RUN cd pcre2-10.39 && CFLAGS="-g -O0" ./configure && make install

# Clone ngx_http_geoip2_module
RUN git clone https://github.com/leev/ngx_http_geoip2_module.git
# we need this hack to make sure that all symbols are exposed; ultimately
# the fuzzer should handle this and we shouldn't need this
RUN sed -i "s/static //g" /root/ngx_http_geoip2_module/ngx_http_geoip2_module.c

# Clone openssl
RUN wget http://www.openssl.org/source/openssl-1.1.1g.tar.gz
RUN tar -zxf openssl-1.1.1g.tar.gz
RUN cd openssl-1.1.1g && CFLAGS="-fsanitize=address -g -O0" ./config && make && make install

# Generate self-gen certificate
RUN { echo ""; echo ""; echo ""; echo ""; echo ""; echo "localhost"; echo ""; } | openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -nodes

# Build Nginx
RUN apt install -y build-essential zlib1g zlib1g-dev libmaxminddb-dev

RUN wget https://nginx.org/download/nginx-1.21.6.tar.gz
RUN tar -xf nginx-1.21.6.tar.gz

RUN cd nginx-1.21.6 && mkdir logs

RUN cd nginx-1.21.6 && ./configure \
        --with-cc-opt="-O0 -g -fsanitize=address" \
        --with-ld-opt="-lpthread -fsanitize=address" \
        --with-pcre \
        --with-stream \
        --with-http_ssl_module \
        --sbin-path=$(pwd)/nginx \
        --conf-path=$(pwd)/conf/nginx.conf \
        --pid-path=$(pwd)/nginx.pid \
        --add-dynamic-module=../ngx_http_geoip2_module
RUN cd nginx-1.21.6 && make modules && make -j

RUN mkdir -p /usr/local/nginx/modules/
RUN mv /root/nginx-1.21.6/objs/ngx_http_geoip2_module.so /usr/local/nginx/modules/ngx_http_geoip_module.so

RUN mkdir -p /usr/local/nginx/logs/
RUN cp -r /root/nginx-1.21.6/html/ /usr/local/nginx/html

# make sure that the regex feature is well covered
RUN sed -i "s/location \//location \~* \//g" /root/nginx-1.21.6/conf/nginx.conf
RUN sed -i "s|server_name  localhost;|server_name   ~^(www\\\.)?(?<domain>.+)$; location / { root   /sites/\$domain; }|g" /root/nginx-1.21.6/conf/nginx.conf

# make sure that ssl feature is well covered
RUN sed -i "21a ssl_protocols       TLSv1 TLSv1.1 TLSv1.2;" /root/nginx-1.21.6/conf/nginx.conf
RUN sed -i "22a ssl_ciphers AES128-SHA:AES256-SHA:RC4-SHA:DES-CBC3-SHA:RC4-MD5;" /root/nginx-1.21.6/conf/nginx.conf
RUN sed -i "23a ssl_certificate /root/cert.pem;" /root/nginx-1.21.6/conf/nginx.conf
RUN sed -i "24a ssl_certificate_key /root/key.pem;" /root/nginx-1.21.6/conf/nginx.conf
RUN sed -i "25a ssl_session_cache   shared:SSL:10m;" /root/nginx-1.21.6/conf/nginx.conf
RUN sed -i "26a ssl_session_timeout 10m;" /root/nginx-1.21.6/conf/nginx.conf
RUN sed -i "42a listen 443 ssl;" /root/nginx-1.21.6/conf/nginx.conf

# generate API description
# note: we use conffuzz from the docker-cache for this step to avoid having to
# redo it every single time we do a change to conffuzz/
COPY ./resources/docker-cache/ /root/
RUN make genapi-nginx
# we get a fresh copy of conffuzz when we actually mount the container
RUN rm -rf /root/conffuzz

WORKDIR /root

# start nginx with -g 'daemon off; error_log stderr debug; master_process off;'
