all: conffuzz examples

mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
mkfile_dir := $(dir $(mkfile_path))
PIN_HOME=$(mkfile_dir)/pintools
PIN_EXAMPLES_HOME=$(PIN_HOME)/source/tools/SimpleExamples/

SUDO := sudo
DEBUG_OPT := -d
STATIC_ANALYIS_OPT := -S

# clone submodules
submodules:
	git submodule update --init

# Build pin framework
pintools:
	tar -xf resources/pin-3.21-98484-ge7cd811fd-gcc-linux.tar.gz
	mv pin-3.21-98484-ge7cd811fd-gcc-linux/ pintools

# Build conffuzz
.PHONY: conffuzz
conffuzz: pintools
	make -C $(mkfile_dir)/conffuzz

.PHONY: conffuzz-dbg
conffuzz-dbg: pintools
	make -C $(mkfile_dir)/conffuzz dbg

# ========================================================================
# Build dev & fuzzing Docker environments
# ========================================================================

devdocker: submodules
	docker build -t conffuzz-dev -f conffuzz-dev.dockerfile .

aspelldocker: devdocker
	docker build -t conffuzz-aspell -f examples/aspell.dockerfile .

magickdocker: devdocker
	docker build -t conffuzz-magick -f examples/magick.dockerfile .

okulardocker: devdocker
	# necessary to run the container
	$(SUDO) apt -y install x11-xserver-utils
	docker build -t conffuzz-okular -f examples/okular.dockerfile .

apachedocker: devdocker
	docker build -t conffuzz-apache -f examples/apache.dockerfile .

redisdocker: devdocker
	docker build -t conffuzz-redis -f examples/redis.dockerfile .

lighttpddocker: devdocker
	docker build -t conffuzz-lighttpd -f examples/lighttpd.dockerfile .

nginxdocker: devdocker
	docker build -t conffuzz-nginx -f examples/nginx.dockerfile .

memcacheddocker: devdocker
	docker build -t conffuzz-memcached -f examples/memcached.dockerfile .

bind9docker: devdocker
	docker build -t conffuzz-bind9 -f examples/bind9.dockerfile .

bzip2docker: devdocker
	docker build -t conffuzz-bzip2 -f examples/bzip2.dockerfile .

sshdocker: devdocker
	docker build -t conffuzz-ssh -f examples/ssh.dockerfile .

sudodocker: devdocker
	docker build -t conffuzz-sudo -f examples/sudo.dockerfile .

filedocker: devdocker
	docker build -t conffuzz-file -f examples/file.dockerfile .

ffmpegdocker: devdocker
	docker build -t conffuzz-ffmpeg -f examples/ffmpeg.dockerfile .

curldocker: devdocker
	docker build -t conffuzz-curl -f examples/curl.dockerfile .

inkscapedocker: devdocker
	docker build -t conffuzz-inkscape -f examples/inkscape.dockerfile .

gitdocker: curldocker # we reuse the curl build here
	docker build -t conffuzz-git -f examples/git.dockerfile .

wiresharkdocker: devdocker
	docker build -t conffuzz-wireshark -f examples/wireshark.dockerfile .

gpadocker: devdocker
	$(SUDO) apt -y install x11-xserver-utils
	docker build -t conffuzz-gpa -f examples/gpa.dockerfile .

firefoxdocker: devdocker
	# necessary to run the container
	$(SUDO) apt -y install x11-xserver-utils
	docker build -t conffuzz-firefox -f examples/firefox.dockerfile .

exifdocker: devdocker
	docker build -t conffuzz-exif -f examples/exif.dockerfile .

squiddocker: devdocker
	docker build -t conffuzz-squid -f examples/squid.dockerfile .

haproxydocker: devdocker
	docker build -t conffuzz-haproxy -f examples/haproxy.dockerfile .

libxmltestdocker: devdocker
	docker build -t conffuzz-libxmltest -f examples/libxmltest.dockerfile .

unikraftdocker: devdocker
	docker build -t conffuzz-unikraft -f examples/unikraft.dockerfile .

rsyncdocker: devdocker
	docker build -t conffuzz-rsync -f examples/rsync.dockerfile .

sudocker: devdocker
	docker build -t conffuzz-su -f examples/su.dockerfile .

.PHONY: docker examples
docker: devdocker magickdocker okulardocker apachedocker redisdocker nginxdocker \
	bzip2docker memcacheddocker bind9docker sshdocker sudodocker filedocker \
	ffmpegdocker curldocker inkscapedocker gitdocker wiresharkdocker \
	gpadocker exifdocker squiddocker aspelldocker haproxydocker \
	libxmltestdocker rsyncdocker sudocker

# ========================================================================
# Start dev & fuzzing Docker environments (shell)
# ========================================================================

examples: docker
	make -C $(mkfile_dir)/examples/lib-example/
	make -C $(mkfile_dir)/examples/app-example/

devshell: devdocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-dev \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

magickshell: magickdocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-magick \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

okularshell: okulardocker
	xhost "+Local:*"
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged --rm \
		-e DISPLAY=$(DISPLAY) -v /tmp/.X11-unix/:/tmp/.X11-unix/ \
                -it conffuzz-okular \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"
	xhost "-Local:*"

apacheshell: apachedocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-apache \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

redisshell: redisdocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-redis \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

lighttpdshell: lighttpddocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-lighttpd \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

nginxshell: nginxdocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-nginx \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

memcachedshell: memcacheddocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-memcached \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

bind9shell: bind9docker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-bind9 \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

bzip2shell: bzip2docker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-bzip2 \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

sshshell: sshdocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-ssh \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

sudoshell: sudodocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-sudo \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

fileshell: filedocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-file \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

ffmpegshell: ffmpegdocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-ffmpeg \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

curlshell: curldocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-curl \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

inkscapeshell: inkscapedocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-inkscape \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

gitshell: gitdocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-git \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

wiresharkshell: wiresharkdocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-wireshark \
		bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

gpashell: gpadocker
	xhost "+Local:*"
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged --rm \
		-e DISPLAY=$(DISPLAY) -v /tmp/.X11-unix/:/tmp/.X11-unix/ \
 		-it conffuzz-gpa \
 		bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"
	xhost "-Local:*"

firefoxshell: firefoxdocker
	xhost "+Local:*"
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged --rm \
		-e DISPLAY=$(DISPLAY) -v /tmp/.X11-unix/:/tmp/.X11-unix/ \
 		-it conffuzz-firefox \
 		bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"
	xhost "-Local:*"

exifshell: exifdocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-exif \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

aspellshell: aspelldocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-aspell \
		bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

squidshell: squiddocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-squid \
                bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

haproxyshell: haproxydocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-haproxy \
		bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

libxmltestshell: libxmltestdocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-libxmltest \
		bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

unikraftshell: unikraftdocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-unikraft \
		bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"
rsyncshell: rsyncdocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-rsync \
		bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

sushell: sudocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-su \
		bash -c "cp -R /mnt/workspace/* /root/ && make -C /root/ properclean && /bin/bash"

# ========================================================================
# API data generation rules
# ========================================================================

# these rules are supposed to be run within Docker containers

# -X means "only generate API files, don't fuzz"

genapi-magick: conffuzz
	mkdir -p api/libgs
	./conffuzz/conffuzz -X $(DEBUG_OPT) -r "gsapi" \
		ghostscript-9.55.0/sodebugbin/libgs.so.9.55 /usr/local/bin/convert
	mv /tmp/conffuzz_functions.txt api/libgs/functions.txt
	mv /tmp/conffuzz_types.txt api/libgs/types.txt
	mkdir -p api/libpng
	./conffuzz/conffuzz -X $(DEBUG_OPT) \
		/usr/local/lib/libpng16.so /usr/local/bin/convert
	mv /tmp/conffuzz_functions.txt api/libpng/functions.txt
	mv /tmp/conffuzz_types.txt api/libpng/types.txt
	mkdir -p api/libtiff
	./conffuzz/conffuzz -X $(DEBUG_OPT) \
		/usr/local/lib/libtiff.so /usr/local/bin/convert
	mv /tmp/conffuzz_functions.txt api/libtiff/functions.txt
	mv /tmp/conffuzz_types.txt api/libtiff/types.txt

genapi-nginx: conffuzz
	mkdir -p api/libpcre2
	./conffuzz/conffuzz -X $(DEBUG_OPT) \
		/usr/local/lib/libpcre2-8.so.0.10.4 ./nginx-1.21.6/objs/nginx
	mv /tmp/conffuzz_functions.txt api/libpcre2/functions.txt
	mv /tmp/conffuzz_types.txt api/libpcre2/types.txt
	mkdir -p api/modgeoip
	./conffuzz/conffuzz -X $(DEBUG_OPT) \
		/usr/local/nginx/modules/ngx_http_geoip_module.so \
		/root/nginx-1.21.6/objs/nginx
	mv /tmp/conffuzz_functions.txt api/modgeoip/functions.txt
	mv /tmp/conffuzz_types.txt api/modgeoip/types.txt
	mkdir -p api/libssl
	./conffuzz/conffuzz -X $(DEBUG_OPT) -l 2 /usr/local/lib/libssl.so.1.1 \
		/usr/local/lib/libcrypto.so.1.1 /root/nginx-1.21.6/objs/nginx
	mv /tmp/conffuzz_functions.txt api/libssl/functions.txt
	mv /tmp/conffuzz_types.txt api/libssl/types.txt
	mkdir -p api/libssl-erim
	./conffuzz/conffuzz -X -x $(DEBUG_OPT) /usr/local/lib/libcrypto.so.1.1 \
		-r '(AES|aesni|asm_AES)_?(ecb|set|cfb1|cbc)?_?(encrypt|decrypt)_?(key|intern|blocks)?' \
		./nginx-1.21.6/objs/nginx
	mv /tmp/conffuzz_functions.txt api/libssl-erim/functions.txt
	mv /tmp/conffuzz_types.txt api/libssl-erim/types.txt

genapi-okular: conffuzz
	mkdir -p api/libpoppler
	./conffuzz/conffuzz -X $(DEBUG_OPT) \
		-r '^(?=.*Poppler)(((?!(Private|Annotation|Sound|Movie|FormField)).)*)$$' \
		-l2 /usr/local/lib/libpoppler.so /usr/local/lib/libpoppler-qt5.so \
		/root/okular/build/bin/okular
	mv /tmp/conffuzz_functions.txt api/libpoppler/functions.txt
	mv /tmp/conffuzz_types.txt api/libpoppler/types.txt
	mkdir -p api/libmarkdown
	./conffuzz/conffuzz -X $(DEBUG_OPT) \
		/usr/lib/libmarkdown.so /root/okular/build/bin/okular
	mv /tmp/conffuzz_functions.txt api/libmarkdown/functions.txt
	mv /tmp/conffuzz_types.txt api/libmarkdown/types.txt

genapi-apache: conffuzz
	mkdir -p api/apache-libmarkdown
	./conffuzz/conffuzz -X $(DEBUG_OPT) \
		/root/discount-2.2.7/libmarkdown.so.2.2.7 ./httpd-2.4.52/httpd
	mv /tmp/conffuzz_functions.txt api/apache-libmarkdown/functions.txt
	mv /tmp/conffuzz_types.txt api/apache-libmarkdown/types.txt
	mkdir -p api/apache-mod-markdown
	./conffuzz/conffuzz -X $(DEBUG_OPT) \
		-L /root/discount-2.2.7/libmarkdown.so.2.2.7 -L ./httpd-2.4.52/httpd \
		/usr/local/apache2/modules/mod_markdown.so ./httpd-2.4.52/httpd
	mv /tmp/conffuzz_functions.txt api/apache-mod-markdown/functions.txt
	mv /tmp/conffuzz_types.txt api/apache-mod-markdown/types.txt

genapi-redis: conffuzz
	mkdir -p api/redis-mod-redisearch
	./conffuzz/conffuzz -X $(DEBUG_OPT) /root/RediSearch/bin/linux-x64-debug/search/redisearch.so \
		-r '^(?=[A-Z]([a-zA-Z]+)(Command|_OnLoad)$$)(((?!ACL).)*)$$' \
		-L /root/redis-6.2.6/src/redis-server \
		/root/redis-6.2.6/src/redis-server
	mv /tmp/conffuzz_functions.txt api/redis-mod-redisearch/functions.txt
	mv /tmp/conffuzz_types.txt api/redis-mod-redisearch/types.txt
	mkdir -p api/redis-mod-redisbloom
	./conffuzz/conffuzz -X $(DEBUG_OPT) /root/RedisBloom/redisbloom.so \
		-L /root/redis-6.2.6/src/redis-server \
		-r '(RedisCommand|_OnLoad)$$'\
		/root/redis-6.2.6/src/redis-server
	mv /tmp/conffuzz_functions.txt api/redis-mod-redisbloom/functions.txt
	mv /tmp/conffuzz_types.txt api/redis-mod-redisbloom/types.txt

genapi-lighttpd: conffuzz
	mkdir -p api/lighttpd-mod-deflate
	./conffuzz/conffuzz -X -x $(DEBUG_OPT) /usr/local/lib/mod_deflate.so \
		-r '^mod_deflate.*'\
		/root/lighttpd-1.4.67/src/lighttpd
	mv /tmp/conffuzz_functions.txt api/lighttpd-mod-deflate/functions.txt
	mv /tmp/conffuzz_types.txt api/lighttpd-mod-deflate/types.txt

genapi-memcached: conffuzz
	mkdir -p api/memcached-libsasl2
	./conffuzz/conffuzz -X -x $(DEBUG_OPT) /usr/local/lib/libsasl2.so.3 \
		./memcached-1.6.17/memcached
	mv /tmp/conffuzz_functions.txt api/memcached-libsasl2/functions.txt
	mv /tmp/conffuzz_types.txt api/memcached-libsasl2/types.txt

genapi-bzip2: conffuzz
	mkdir -p api/bzip2-libbz2
	./conffuzz/conffuzz -X $(DEBUG_OPT) /lib/x86_64-linux-gnu/libbz2.so.1 \
		./bzip2-1.0.8/bzip2-shared
	mv /tmp/conffuzz_functions.txt api/bzip2-libbz2/functions.txt
	mv /tmp/conffuzz_types.txt api/bzip2-libbz2/types.txt

genapi-ssh: conffuzz
	mkdir -p api/ssh-libcrypto
	./conffuzz/conffuzz -X $(DEBUG_OPT) /usr/local/lib/libcrypto.so \
		/usr/local/bin/ssh
	mv /tmp/conffuzz_functions.txt api/ssh-libcrypto/functions.txt
	mv /tmp/conffuzz_types.txt api/ssh-libcrypto/types.txt

genapi-sudo: conffuzz
	mkdir -p api/sudo-libapparmor
	./conffuzz/conffuzz -X $(DEBUG_OPT) /usr/local/lib/libapparmor.so \
		/usr/local/bin/sudo
	mv /tmp/conffuzz_functions.txt api/sudo-libapparmor/functions.txt
	mv /tmp/conffuzz_types.txt api/sudo-libapparmor/types.txt
	mkdir -p api/sudo-authapi
	./conffuzz/conffuzz -X $(DEBUG_OPT) /usr/local/libexec/sudo/sudoers.so \
		-L /usr/local/bin/sudo -x \
		-r "sudo_auth_((init)|(setup)|(cleanup)|(init_quiet)|(approval)|(begin_session)|(end_session)|(verify))$$" \
		/usr/local/bin/sudo
	mv /tmp/conffuzz_functions.txt api/sudo-authapi/functions.txt
	mv /tmp/conffuzz_types.txt api/sudo-authapi/types.txt
	mkdir -p api/sudo-modsudoers
	./conffuzz/conffuzz -x -X $(DEBUG_OPT) /usr/local/libexec/sudo/sudoers.so \
		-r "^sudoers.*" /usr/local/bin/sudo
	mv /tmp/conffuzz_functions.txt api/sudo-modsudoers/functions.txt
	mv /tmp/conffuzz_types.txt api/sudo-modsudoers/types.txt

genapi-inkscape: conffuzz
	mkdir -p api/inkscape-libpoppler
	./conffuzz/conffuzz -X $(DEBUG_OPT) -l2 /usr/local/lib/libpoppler-glib.so.8 /usr/local/lib/libpoppler.so \
		-r "poppler_" \
		/root/inkscape-INKSCAPE_1_1_2/build/bin/inkscape
	mv /tmp/conffuzz_functions.txt api/inkscape-libpoppler/functions.txt
	mv /tmp/conffuzz_types.txt api/inkscape-libpoppler/types.txt
	mkdir -p api/inkscape-libpng
	./conffuzz/conffuzz -X $(DEBUG_OPT) /usr/local/lib/libpng16.so \
		/root/inkscape-INKSCAPE_1_1_2/build/bin/inkscape
	mv /tmp/conffuzz_functions.txt api/inkscape-libpng/functions.txt
	mv /tmp/conffuzz_types.txt api/inkscape-libpng/types.txt

genapi-git: conffuzz
	mkdir -p api/git-libcurl
	./conffuzz/conffuzz -X $(DEBUG_OPT) /usr/local/lib/libcurl.so.4 \
		/root/git-2.35.1/git-http-push
	mv /tmp/conffuzz_functions.txt api/git-libcurl/functions.txt
	mv /tmp/conffuzz_types.txt api/git-libcurl/types.txt
	mkdir -p api/git-libpcre
	./conffuzz/conffuzz -X $(DEBUG_OPT) /usr/local/lib/libpcre2-8.so \
		/root/git-2.35.1/git-grep
	mv /tmp/conffuzz_functions.txt api/git-libpcre/functions.txt
	mv /tmp/conffuzz_types.txt api/git-libpcre/types.txt

genapi-haproxy: conffuzz
	mkdir -p api/haproxy-libslz
	./conffuzz/conffuzz -X $(DEBUG_OPT) /usr/local/lib/libslz.so \
		./haproxy-ss-20221026/haproxy
	mv /tmp/conffuzz_functions.txt api/haproxy-libslz/functions.txt
	mv /tmp/conffuzz_types.txt api/haproxy-libslz/types.txt

genapi-bind9: conffuzz
	mkdir -p api/bind9-libxml2
	./conffuzz/conffuzz -X $(DEBUG_OPT) /usr/local/lib/libxml2.so.2 \
		-r "^xml" ./bind-9.18.8/bin/named/.libs/named
	mv /tmp/conffuzz_functions.txt api/bind9-libxml2/functions.txt
	mv /tmp/conffuzz_types.txt api/bind9-libxml2/types.txt

genapi-squid: conffuzz
	mkdir -p api/squid-libxml2
	./conffuzz/conffuzz -X $(DEBUG_OPT) /usr/local/lib/libxml2.so.2.10.3 \
		-L /usr/local/squid/sbin/squid \
		-r "^(xml|html)" /usr/local/squid/sbin/squid
	mv /tmp/conffuzz_functions.txt api/squid-libxml2/functions.txt
	mv /tmp/conffuzz_types.txt api/squid-libxml2/types.txt
	mkdir -p api/squid-libexpat
	./conffuzz/conffuzz -X $(DEBUG_OPT) /usr/local/lib/libexpat.so.1.8.10 \
		-L /usr/local/squid/sbin/squid \
		-r "^XML_.*" /usr/local/squid/sbin/squid
	mv /tmp/conffuzz_functions.txt api/squid-libexpat/functions.txt
	mv /tmp/conffuzz_types.txt api/squid-libexpat/types.txt

genapi-rsync: conffuzz
	mkdir -p api/rsync-libpopt
	./conffuzz/conffuzz -X $(DEBUG_OPT) /usr/local/lib/libpopt.so.0.0.2 \
		-L /root/rsync-3.2.7/rsync \
		-r "^popt.*" /root/rsync-3.2.7/rsync
	mv /tmp/conffuzz_functions.txt api/rsync-libpopt/functions.txt
	mv /tmp/conffuzz_types.txt api/rsync-libpopt/types.txt

genapi-su: conffuzz
	mkdir -p api/su-libaudit
	./conffuzz/conffuzz -X $(DEBUG_OPT) /usr/local/lib/libaudit.so.1 \
		-L /root/shadow-4.13/src/su \
		-r "^audit_.*" /root/shadow-4.13/src/su
	mv /tmp/conffuzz_functions.txt api/su-libaudit/functions.txt
	mv /tmp/conffuzz_types.txt api/su-libaudit/types.txt

# TODO similar rules for other apps

# ========================================================================
# Pin overhead measurement rules
# ========================================================================

pin-overhead-nginx: nginxdocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-nginx \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 apt install -y datamash bc && \
			 /root/benchmarks/nginx-pin-overhead.sh"

pin-overhead-redis: redisdocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-redis \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 apt install -y datamash bc && \
			 /root/benchmarks/redis-pin-overhead.sh"

pin-overhead-xmltest: libxmltestdocker
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-libxmltest \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 apt install -y datamash bc && \
			 /root/benchmarks/libxmltests-pin-overhead.sh"

# ========================================================================
# Push-button fuzzing rules
# ========================================================================

FUZZ_ITERATIONS := 400

fuzz-okular: okulardocker
	mkdir -p fuzz/sandbox/okular-libpoppler
	xhost "+Local:*"
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm \
		-e DISPLAY=$(DISPLAY) -v /tmp/.X11-unix/:/tmp/.X11-unix/ conffuzz-okular \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 QT_DEBUG_PLUGINS=1 QT_QPA_PLATFORM=xcb ./conffuzz/conffuzz $(DEBUG_OPT) \
				-r '^(?=.*Poppler)(((?!(Private|Annotation|Sound|Movie|FormField)).)*)$$' \
				-F api/libpoppler/functions.txt -G api/libpoppler/types.txt \
				$(STATIC_ANALYIS_OPT) -O /mnt/workspace/fuzz/sandbox/okular-libpoppler \
				-i $(FUZZ_ITERATIONS) -l2 -T60 /usr/local/lib/libpoppler.so \
				/usr/local/lib/libpoppler-qt5.so \
				/root/okular/build/bin/okular -- foo.pdf"
	xhost "-Local:*"
	mkdir -p fuzz/sandbox/okular-libmarkdown
	xhost "+Local:*"
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm \
		-e DISPLAY=$(DISPLAY) -v /tmp/.X11-unix/:/tmp/.X11-unix/ conffuzz-okular \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 QT_DEBUG_PLUGINS=1 QT_QPA_PLATFORM=xcb ./conffuzz/conffuzz $(DEBUG_OPT) \
				-F api/libmarkdown/functions.txt -G api/libmarkdown/types.txt \
				-O /mnt/workspace/fuzz/sandbox/okular-libmarkdown -i $(FUZZ_ITERATIONS) \
				$(STATIC_ANALYIS_OPT) -T60 /usr/lib/libmarkdown.so \
				/root/okular/build/bin/okular -- foo.md"
	xhost "-Local:*"

fuzz-magick: magickdocker
	mkdir -p fuzz/sandbox/magick-libghostscript
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-magick \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz -r "gsapi" $(DEBUG_OPT) \
				-F api/libgs/functions.txt -G api/libgs/types.txt \
				-O /mnt/workspace/fuzz/sandbox/magick-libghostscript -i $(FUZZ_ITERATIONS) \
				$(STATIC_ANALYIS_OPT) -T120 ghostscript-9.55.0/sodebugbin/libgs.so.9.55 \
				/usr/local/bin/convert -- foo.ps foo.pdf"
	mkdir -p fuzz/sandbox/magick-libpng
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-magick \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) \
				-F api/libpng/functions.txt -G api/libpng/types.txt \
				-O /mnt/workspace/fuzz/sandbox/magick-libpng -i $(FUZZ_ITERATIONS) \
				$(STATIC_ANALYIS_OPT) -T120 /usr/local/lib/libpng16.so \
				/usr/local/bin/convert -- foo.ps foo.png"
	mkdir -p fuzz/sandbox/magick-libtiff
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-magick \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) \
				-F api/libpng/functions.txt -G api/libpng/types.txt \
				-O /mnt/workspace/fuzz/sandbox/magick-libtiff -i $(FUZZ_ITERATIONS) \
				$(STATIC_ANALYIS_OPT) -T120 /usr/local/lib/libtiff.so \
				/usr/local/bin/convert -- foo.ps foo.tiff"

fuzz-apache: apachedocker
	mkdir -p fuzz/sandbox/apache-libmarkdown
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-apache \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) /root/discount-2.2.7/libmarkdown.so.2.2.7 \
				-F api/apache-libmarkdown/functions.txt -G api/apache-libmarkdown/types.txt \
				$(STATIC_ANALYIS_OPT) -T50 -O /mnt/workspace/fuzz/sandbox/apache-libmarkdown \
				-i $(FUZZ_ITERATIONS) ./httpd-2.4.52/httpd -t ./examples/apachescript.sh -- -X"
	mkdir -p fuzz/sandbox/apache-mod-markdown
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-apache \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) /usr/local/apache2/modules/mod_markdown.so \
				-L /root/discount-2.2.7/libmarkdown.so.2.2.7 -L ./httpd-2.4.52/httpd \
				-F api/apache-mod-markdown/functions.txt -G api/apache-mod-markdown/types.txt \
				$(STATIC_ANALYIS_OPT) -T50 -O /mnt/workspace/fuzz/sandbox/apache-mod-markdown \
				-i $(FUZZ_ITERATIONS) ./httpd-2.4.52/httpd -t ./examples/apachescript.sh -- -X"

fuzz-redis: redisdocker
	mkdir -p fuzz/sandbox/redis-mod-redisearch
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-redis \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) -m /root/RediSearch/bin/linux-x64-debug/search/redisearch.so \
				-L /root/redis-6.2.6/src/redis-server \
				-F api/redis-mod-redisearch/functions.txt -G api/redis-mod-redisearch/types.txt \
				-r '^(?=[A-Z]([a-zA-Z]+)(Command|_OnLoad)$$)(((?!ACL).)*)$$'\
				-O /mnt/workspace/fuzz/sandbox/redis-mod-redisearch \
				-i $(FUZZ_ITERATIONS) $(STATIC_ANALYIS_OPT) /root/redis-6.2.6/src/redis-server \
				-t examples/redis-redisearch.sh -- \
				/root/redis-6.2.6/redis.conf \
				--loadmodule /root/RediSearch/bin/linux-x64-debug/search/redisearch.so"
	mkdir -p fuzz/sandbox/redis-mod-redisbloom
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-redis \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) -m /root/RedisBloom/redisbloom.so \
				-L /root/redis-6.2.6/src/redis-server \
				-F api/redis-mod-redisbloom/functions.txt -G api/redis-mod-redisbloom/types.txt \
				-r '(RedisCommand|_OnLoad)$$'\
				-O /mnt/workspace/fuzz/sandbox/redis-mod-redisbloom \
				-i $(FUZZ_ITERATIONS) $(STATIC_ANALYIS_OPT) /root/redis-6.2.6/src/redis-server \
				-t examples/redis-redisbloom.sh -- \
				/root/redis-6.2.6/redis.conf \
				--loadmodule /root/RedisBloom/redisbloom.so"

fuzz-lighttpd: lighttpddocker
	mkdir -p fuzz/sandbox/lighttpd-mod-deflate
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-lighttpd \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) -m -x /usr/local/lib/mod_deflate.so \
				-F api/lighttpd-mod-deflate/functions.txt -G api/lighttpd-mod-deflate/types.txt \
				-r '^mod_deflate.*' -O /mnt/workspace/fuzz/sandbox/lighttpd-mod-deflate \
				-i $(FUZZ_ITERATIONS) $(STATIC_ANALYIS_OPT) /root/lighttpd-1.4.67/src/lighttpd \
				-t examples/lighttpdscript.sh -- -f lighttpd.conf -D"

fuzz-nginx-erim: nginxdocker
	mkdir -p fuzz/safebox/nginx-libssl-erim
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-nginx \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			make -C /root/ properclean && \
			make -C /root/ conffuzz && \
			./conffuzz/conffuzz -x -R $(DEBUG_OPT) /usr/local/lib/libcrypto.so.1.1 \
				-r '(AES|aesni|asm_AES)_?(ecb|set|cfb1|cbc)?_?(encrypt|decrypt)_?(key|intern|blocks)?' \
				-F api/libssl-erim/functions.txt -G api/libssl-erim/types.txt \
				-O /mnt/workspace/fuzz/safebox/nginx-libssl-erim -i $(FUZZ_ITERATIONS) \
				$(STATIC_ANALYIS_OPT) -t examples/nginxscript-ssl.sh ./nginx-1.21.6/objs/nginx  -- \
				-g 'daemon off; error_log stderr debug; master_process off;'"

fuzz-memcached: memcacheddocker
	mkdir -p fuzz/safebox/memcached-libsasl2
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-memcached \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			make -C /root/ properclean && \
			make -C /root/ conffuzz && \
			./conffuzz/conffuzz -x -R $(DEBUG_OPT) /usr/local/lib/libsasl2.so.3 \
				-F api/memcached-libsasl2/functions.txt -G api/memcached-libsasl2/types.txt \
				-O /mnt/workspace/fuzz/safebox/memcached-libsasl2 -i $(FUZZ_ITERATIONS) \
				$(STATIC_ANALYIS_OPT) -t examples/memcachedscript.sh ./memcached-1.6.17/memcached  -- \
				-l 127.0.0.1 -p 11211 -m 64 -S -vvv -u root"

fuzz-bind9: bind9docker
	mkdir -p fuzz/sandbox/bind9-libxml2
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-bind9 \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			make -C /root/ properclean && \
			make -C /root/ conffuzz && \
			./conffuzz/conffuzz -x $(DEBUG_OPT) /usr/local/lib/libxml2.so.2 \
				-O /mnt/workspace/fuzz/sandbox/bind9-libxml2 -i $(FUZZ_ITERATIONS) \
				-r "xml" \
				-F api/bind9-libxml2/functions.txt -G api/bind9-libxml2/types.txt \
				$(STATIC_ANALYIS_OPT) -t examples/bindscript.sh \
				./bind-9.18.8/bin/named/.libs/named -- -g"

fuzz-nginx: nginxdocker
	mkdir -p fuzz/sandbox/nginx-libpcre2
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-nginx \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) /usr/local/lib/libpcre2-8.so.0.10.4 \
				-F api/libpcre2/functions.txt -G api/libpcre2/types.txt \
				-O /mnt/workspace/fuzz/sandbox/nginx-libpcre2 -i $(FUZZ_ITERATIONS) \
				$(STATIC_ANALYIS_OPT) ./nginx-1.21.6/objs/nginx -t examples/nginxscript.sh -- \
				-g 'daemon off; error_log stderr debug; master_process off;'"
	mkdir -p fuzz/sandbox/nginx-mod-geopip
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-nginx \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) \
				/usr/local/nginx/modules/ngx_http_geoip_module.so \
				-F api/modgeoip/functions.txt -G api/modgeoip/types.txt \
				-O /mnt/workspace/fuzz/sandbox/nginx-mod-geopip -i $(FUZZ_ITERATIONS) \
				$(STATIC_ANALYIS_OPT) /root/nginx-1.21.6/objs/nginx -t examples/nginxscript.sh -- \
				-g 'load_module modules/ngx_http_geoip_module.so; daemon off; \
					error_log stderr debug; master_process off;'"
	mkdir -p fuzz/safebox/nginx-libssl
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-nginx \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz -R $(DEBUG_OPT) -l 2 /usr/local/lib/libssl.so.1.1 \
				/usr/local/lib/libcrypto.so.1.1 \
				-F api/libssl/functions.txt -G api/libssl/types.txt \
				-O /mnt/workspace/fuzz/safebox/nginx-libssl -i $(FUZZ_ITERATIONS) \
				$(STATIC_ANALYIS_OPT) -t examples/nginxscript-ssl.sh ./nginx-1.21.6/objs/nginx -- \
				-g 'daemon off; error_log stderr debug; master_process off;'"

fuzz-bzip2: bzip2docker
	mkdir -p fuzz/sandbox/bzip2-libbz2
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-bzip2 \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) /lib/x86_64-linux-gnu/libbz2.so.1 \
				-F api/bzip2-libbz2/functions.txt -G api/bzip2-libbz2/types.txt \
				$(STATIC_ANALYIS_OPT) -O /mnt/workspace/fuzz/sandbox/bzip2-libbz2 -i $(FUZZ_ITERATIONS) \
				./bzip2-1.0.8/bzip2-shared -- -f -k -d example.bz2"

fuzz-ssh: sshdocker
	mkdir -p fuzz/safebox/ssh-libcrypto
	# first start ssh server on the local machine, then fuzz
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-ssh \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 service ssh start && \
			 ./conffuzz/conffuzz -R $(DEBUG_OPT) /usr/local/lib/libcrypto.so \
				-F api/ssh-libcrypto/functions.txt -G api/ssh-libcrypto/types.txt \
				$(STATIC_ANALYIS_OPT) -O /mnt/workspace/fuzz/safebox/ssh-libcrypto -i $(FUZZ_ITERATIONS) \
				/usr/local/bin/ssh -- -o StrictHostKeyChecking=no localhost ls /root"

fuzz-sudo: sudodocker
	mkdir -p fuzz/safebox/sudo-libapparmor
	# first enable apparmor, then go through root password prompt, then fuzz
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-sudo \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./start-apparmor.sh && \
			 echo "x" | ASAN_OPTIONS=detect_leaks=0 \
				LD_PRELOAD=/lib/x86_64-linux-gnu/libcrypt.so.1.1.0 sudo -S man && \
			 ./conffuzz/conffuzz -R $(DEBUG_OPT) /usr/local/lib/libapparmor.so \
				-F api/sudo-libapparmor/functions.txt -G api/sudo-libapparmor/types.txt \
				$(STATIC_ANALYIS_OPT) -O /mnt/workspace/fuzz/safebox/sudo-libapparmor \
				-i $(FUZZ_ITERATIONS) /usr/local/bin/sudo -- man"
	mkdir -p fuzz/safebox/sudo-authapi
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-sudo \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 echo "x" | ASAN_OPTIONS=detect_leaks=0 \
				LD_PRELOAD=/lib/x86_64-linux-gnu/libcrypt.so.1.1.0 sudo -S man && \
			 ./conffuzz/conffuzz -R $(DEBUG_OPT) /usr/local/libexec/sudo/sudoers.so \
				-F api/sudo-authapi/functions.txt -G api/sudo-authapi/types.txt \
				$(STATIC_ANALYIS_OPT) -O /mnt/workspace/fuzz/safebox/sudo-authapi \
				-i $(FUZZ_ITERATIONS) /usr/local/bin/sudo -- man"
	mkdir -p fuzz/safebox/sudo-modsudoers
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-sudo \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 echo "x" | ASAN_OPTIONS=detect_leaks=0 \
				LD_PRELOAD=/lib/x86_64-linux-gnu/libcrypt.so.1.1.0 sudo -S man && \
			 ./conffuzz/conffuzz -R $(DEBUG_OPT) /usr/local/libexec/sudo/sudoers.so \
				-F api/sudo-modsudoers/functions.txt -G api/sudo-modsudoers/types.txt \
				$(STATIC_ANALYIS_OPT) -O /mnt/workspace/fuzz/safebox/sudo-modsudoers \
				-i $(FUZZ_ITERATIONS) /usr/local/bin/sudo -- man"

fuzz-rsync: rsyncdocker
	mkdir -p fuzz/sandbox/rsync-libpopt
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-rsync \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) /usr/local/lib/libpopt.so.0.0.2 \
				$(STATIC_ANALYIS_OPT) -O /mnt/workspace/fuzz/sandbox/rsync-libpopt \
				-F api/rsync-libpopt/functions.txt -G api/rsync-libpopt/types.txt \
				-i $(FUZZ_ITERATIONS) /usr/local/bin/rsync -- \
				--daemon -g /root/README.md /root/NDSS"

fuzz-file: filedocker
	mkdir -p fuzz/sandbox/file-libmagic
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-file \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) /usr/lib/x86_64-linux-gnu/libmagic.so.1 \
				$(STATIC_ANALYIS_OPT) -O /mnt/workspace/fuzz/sandbox/file-libmagic \
				-i $(FUZZ_ITERATIONS) /usr/local/bin/file -- README.md \
				/usr/lib/x86_64-linux-gnu/libmagic.so.1 /usr/local/bin/file"

fuzz-ffmpeg: ffmpegdocker
	mkdir -p fuzz/sandbox/ffmpeg-libavcodec
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-ffmpeg \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz -r "avcodec_" $(DEBUG_OPT) /usr/local/lib/libavcodec.so \
				$(STATIC_ANALYIS_OPT) -T120 -O /mnt/workspace/fuzz/sandbox/ffmpeg-libavcodec \
				-i $(FUZZ_ITERATIONS) /root/ffmpeg-5.0/ffprobe -- foo.avi"
	mkdir -p fuzz/sandbox/ffmpeg-libavfilter
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-ffmpeg \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz -r "avfilter_" $(DEBUG_OPT) /usr/local/lib/libavfilter.so \
				$(STATIC_ANALYIS_OPT) -T120 -O /mnt/workspace/fuzz/sandbox/ffmpeg-libavfilter \
				-i $(FUZZ_ITERATIONS) /root/ffmpeg-5.0/ffprobe -- foo.avi"
	mkdir -p fuzz/sandbox/ffmpeg-libavformat
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-ffmpeg \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz -r "avformat_" $(DEBUG_OPT) /usr/local/lib/libavformat.so \
				$(STATIC_ANALYIS_OPT) -T120 -O /mnt/workspace/fuzz/sandbox/ffmpeg-libavformat \
				-i $(FUZZ_ITERATIONS) /root/ffmpeg-5.0/ffprobe -- foo.avi"

fuzz-curl: curldocker
	mkdir -p fuzz/sandbox/curl-libnghttp2
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-curl \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) /usr/local/lib/libnghttp2.so.14 \
				$(STATIC_ANALYIS_OPT) -O /mnt/workspace/fuzz/sandbox/curl-libnghttp2 \
				-i $(FUZZ_ITERATIONS) /usr/local/bin/curl -- --http2 https://google.com"
	mkdir -p fuzz/safebox/curl-libssl
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-curl \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz -R $(DEBUG_OPT) -l 2 /usr/local/lib/libssl.so.1.1 \
				/usr/local/lib/libcrypto.so.1.1 \
				$(STATIC_ANALYIS_OPT) -O /mnt/workspace/fuzz/safebox/curl-libssl -i $(FUZZ_ITERATIONS) \
				/usr/local/bin/curl -- https://google.com"

fuzz-inkscape: inkscapedocker
	mkdir -p fuzz/sandbox/inkscape-libpoppler
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-inkscape \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz -r "poppler_" $(DEBUG_OPT) -T30 $(STATIC_ANALYIS_OPT) \
				-F api/inkscape-libpoppler/functions.txt -G api/inkscape-libpoppler/types.txt \
				-O /mnt/workspace/fuzz/sandbox/inkscape-libpoppler -i $(FUZZ_ITERATIONS) \
				-l 2 /usr/local/lib/libpoppler-glib.so.8 \
				/usr/local/lib/libpoppler.so \
				/root/inkscape-INKSCAPE_1_1_2/build/bin/inkscape \
				-- --pdf-poppler -o foo.svg foo.pdf"
	mkdir -p fuzz/sandbox/inkscape-libpng
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-inkscape \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz -T60 $(DEBUG_OPT) /usr/local/lib/libpng16.so \
				$(STATIC_ANALYIS_OPT) \
				-F api/inkscape-libpng/functions.txt -G api/inkscape-libpng/types.txt \
				-O /mnt/workspace/fuzz/sandbox/inkscape-libpng -i $(FUZZ_ITERATIONS) \
				/root/inkscape-INKSCAPE_1_1_2/build/bin/inkscape \
				-- -o foo.png foo.pdf"

fuzz-git: gitdocker
	mkdir -p fuzz/sandbox/git-libcurl
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-git \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 cd /root/repo-example && \
			 /root/conffuzz/conffuzz $(DEBUG_OPT) /usr/local/lib/libcurl.so.4 \
				-F /root/api/git-libcurl/functions.txt -G /root/api/git-libcurl/types.txt \
				-O /mnt/workspace/fuzz/sandbox/git-libcurl -i $(FUZZ_ITERATIONS) \
				$(STATIC_ANALYIS_OPT) /root/git-2.35.1/git-http-push -- \
				https://github.com/project-flexos/sqlite-splitsrc.git"
	mkdir -p fuzz/sandbox/git-libpcre
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-git \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 cd /root/repo-example && \
			 /root/conffuzz/conffuzz $(DEBUG_OPT) /usr/local/lib/libpcre2-8.so \
				-F /root/api/git-libpcre/functions.txt -G /root/api/git-libpcre/types.txt \
				$(STATIC_ANALYIS_OPT) -O /mnt/workspace/fuzz/sandbox/git-libpcre -i $(FUZZ_ITERATIONS) \
				/root/git-2.35.1/git-grep -- -F 'tcl'"

fuzz-gpa: gpadocker
	mkdir -p fuzz/safebox/gpa-libgpgme
	xhost "+Local:*"
		docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm \
		-e DISPLAY=$(DISPLAY) -v /tmp/.X11-unix/:/tmp/.X11-unix/ conffuzz-gpa \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 cp /root/gpgme-1.15.1/src/.libs/libgpgme.so.11 /usr/lib/x86_64-linux-gnu/ && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) -R -T60 /usr/local/lib/x86_64-linux-gnu/libgpgme.so.11 \
				$(STATIC_ANALYIS_OPT) -O /mnt/workspace/fuzz/safebox/gpa-libgpgme \
				-i $(FUZZ_ITERATIONS) /root/gpa-0.10.0/src/gpa -- -k"
	xhost "-Local:*"

fuzz-gpg: gpadocker
	mkdir -p fuzz/safebox/gpg-libgcrypt
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm \
		-e DISPLAY=$(DISPLAY) -v /tmp/.X11-unix/:/tmp/.X11-unix/ conffuzz-gpa \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			make -C /root/ properclean && \
			make -C /root/ conffuzz && \
			cp /root/gpgme-1.15.1/src/.libs/libgpgme.so.11 /usr/lib/x86_64-linux-gnu/ && \
			./conffuzz/conffuzz $(DEBUG_OPT) -R /usr/local/lib/x86_64-linux-gnu/libgcrypt.so.20.3.2 \
				$(STATIC_ANALYIS_OPT) -O /mnt/workspace/fuzz/safebox/gpg-libgcrypt -r "^gcry_.*" \
				-i $(FUZZ_ITERATIONS) /usr/local/bin/gpg -- --batch --gen-key /root/ndss-example.batch"

fuzz-openssl:
	# TODO
	# mkdir -p fuzz/safebox/opensslcli

fuzz-wireshark: wiresharkdocker
	mkdir -p fuzz/sandbox/wireshark-libpcap
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-wireshark \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) /usr/local/lib/libpcap.so.1.10.1 \
				-O /mnt/workspace/fuzz/sandbox/wireshark-libpcap \
				$(STATIC_ANALYIS_OPT) -i $(FUZZ_ITERATIONS) \
				-t examples/wiresharkscript.sh /root/wireshark_build/run/dumpcap"
	mkdir -p fuzz/sandbox/wireshark-libzlib
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-wireshark \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) /usr/local/lib/libz.so.1.2.12 \
				-O /mnt/workspace/fuzz/sandbox/wireshark-libzlib \
				$(STATIC_ANALYIS_OPT) -i $(FUZZ_ITERATIONS) \
				/root/wireshark_build/run/tshark -- -r \
				/root/wireshark-3.4.12/test/captures/grpc_stream_reassembly_sample.pcapng.gz"

fuzz-exif: exifdocker
	mkdir -p fuzz/sandbox/exif-libexif
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-exif \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) /usr/local/lib/libexif.so.12 \
				-O /mnt/workspace/fuzz/sandbox/exif-libexif -i $(FUZZ_ITERATIONS) \
				$(STATIC_ANALYIS_OPT) /usr/local/bin/exif -- \
				-l -e -d --show-mnote /root/exif_sample.jpg"

fuzz-aspell: aspelldocker
	mkdir -p fuzz/sandbox/aspell-libaspell
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-aspell \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			 make -C /root/ properclean && \
			 make -C /root/ conffuzz && \
			 ./conffuzz/conffuzz $(DEBUG_OPT) /usr/local/lib/libaspell.so.15 \
				-O /mnt/workspace/fuzz/sandbox/aspell-libaspell -i $(FUZZ_ITERATIONS) \
				$(STATIC_ANALYIS_OPT) -t /root/examples/aspellscript.sh /usr/local/bin/aspell"

fuzz-squid: squiddocker
	mkdir -p fuzz/sandbox/squid-libxml2
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-squid \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			make -C /root/ properclean && \
			make -C /root/ conffuzz && \
			service nginx start && \
			ASAN_OPTIONS=new_delete_type_mismatch=0 ./conffuzz/conffuzz $(DEBUG_OPT) \
				-F api/squid-libxml2/functions.txt -G api/squid-libxml2/types.txt \
				/usr/local/lib/libxml2.so.2.10.3 \
				-O /mnt/workspace/fuzz/sandbox/squid-libxml2 -i $(FUZZ_ITERATIONS) \
				$(STATIC_ANALYIS_OPT) -t /root/examples/squidscript.sh \
				/usr/local/squid/sbin/squid -- -N -C -f /usr/local/squid/etc/squid.conf"
	# we need to enable libexpat in the configuration
	mkdir -p fuzz/sandbox/squid-libexpat
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it \
		--rm conffuzz-squid \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			make -C /root/ properclean && \
			make -C /root/ conffuzz && \
			sed -i "s/libxml2/libexpat/g" /usr/local/squid/etc/squid.conf && \
			service nginx start && \
			ASAN_OPTIONS=new_delete_type_mismatch=0 ./conffuzz/conffuzz $(DEBUG_OPT) \
				-F api/squid-libexpat/functions.txt -G api/squid-libexpat/types.txt \
				/usr/local/lib/libexpat.so.1.8.10 \
				-O /mnt/workspace/fuzz/sandbox/squid-libexpat -i $(FUZZ_ITERATIONS) \
				$(STATIC_ANALYIS_OPT) -t /root/examples/squidscript.sh \
				/usr/local/squid/sbin/squid -- -N -C -f /usr/local/squid/etc/squid.conf"

fuzz-haproxy: haproxydocker
	mkdir -p fuzz/sandbox/haproxy-libslz
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-haproxy \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			make -C /root/ properclean && \
			make -C /root/ conffuzz && \
			service apache2 start && \
			./conffuzz/conffuzz $(DEBUG_OPT) /usr/local/lib/libslz.so \
				-F api/haproxy-libslz/functions.txt -G api/haproxy-libslz/types.txt \
				-O /mnt/workspace/fuzz/sandbox/haproxy-libslz -i $(FUZZ_ITERATIONS) \
				$(STATIC_ANALYIS_OPT) -t examples/haproxyscript.sh \
				./haproxy-ss-20221026/haproxy -- -f /etc/haproxy/ -d"

fuzz-libxmltest: libxmltestdocker
	mkdir -p fuzz/sandbox/libxmltest-libxml2
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-libxmltest \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			make -C /root/ properclean && \
			make -C /root/ conffuzz && \
			./conffuzz/conffuzz $(DEBUG_OPT) /usr/local/lib/libxml2.so.2.10.3 -r "^xmlTextWriter.*" \
				-O /mnt/workspace/fuzz/sandbox/libxmltest-libxml2 -i $(FUZZ_ITERATIONS) \
				$(STATIC_ANALYIS_OPT) /root/libxml2-2.10.3/.libs/testapi"

fuzz-su: sudocker
	mkdir -p fuzz/sandbox/su-libaudit
	docker run --volume $(CURDIR):/mnt/workspace:rw --privileged -it --rm conffuzz-su \
		bash -c "cp -R /mnt/workspace/* /root/ && \
			make -C /root/ properclean && \
			make -C /root/ conffuzz && \
			./conffuzz/conffuzz $(DEBUG_OPT) /usr/local/lib/libaudit.so.1 \
				-F api/su-libaudit/functions.txt -G api/su-libaudit/types.txt \
				-O /mnt/workspace/fuzz/sandbox/su-libaudit -i $(FUZZ_ITERATIONS) \
				$(STATIC_ANALYIS_OPT) /root/shadow-4.13/src/su -- -c 'echo "hello"' root"

.PHONY: fuzz
fuzz: clean fuzz-okular fuzz-magick fuzz-apache fuzz-redis fuzz-lighttpd fuzz-nginx-erim \
	fuzz-nginx fuzz-bzip2 fuzz-sudo fuzz-file fuzz-ffmpeg fuzz-curl fuzz-inkscape \
	fuzz-git fuzz-gpa fuzz-wireshark fuzz-exif fuzz-aspell fuzz-haproxy fuzz-bind9 \
	fuzz-gpg fuzz-libxmltest fuzz-su

# ========================================================================
# Clean rules
# ========================================================================

clean:
	make -C $(mkfile_dir)/examples/lib-example clean
	make -C $(mkfile_dir)/examples/app-example clean
	make -C $(mkfile_dir)/conffuzz clean
	rm -rf pintools fuzz

properclean: clean
	rm -rf crashes
