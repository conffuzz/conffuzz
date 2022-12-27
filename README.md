# ConfFuzz - Fuzzing for Interface Vulnerabilities

[![](https://img.shields.io/badge/arXiv-paper-red)](https://arxiv.org/abs/2212.12904)

This repository contains the main tree of our ConfFuzz proof-of-concept.
ConfFuzz is an in-memory fuzzer aimed at detecting interface vulnerabilities in
compartmentalized contexts. ConfFuzz is a cooperation between the University of
Manchester, University Politehnica of Bucharest, Rice University, and
Unikraft.io. It has been accepted to appear in NDSS'23.

> **Abstract**: Least-privilege separation decomposes applications
> into compartments limited to accessing only what they need.
> When compartmentalizing existing software, many approaches
> neglect securing the new inter-compartment interfaces, although
> what used to be a function call from/to a trusted component is
> now potentially a targeted attack from a malicious compartment.
> This results in an entire class of security bugs: Compartment
> Interface Vulnerabilities (CIVs).
>
> This paper provides an in-depth study of CIVs. We taxonomize
> these issues and show that they affect all known compartmentalization
> approaches. We propose ConfFuzz, an in-memory fuzzer
> specialized to detect CIVs at possible compartment boundaries.
> We apply ConfFuzz to a set of 25 popular applications and
> 36 possible compartment APIs, to uncover a wide data-set of
> 629 vulnerabilities. We systematically study these issues, and
> extract numerous insights on the prevalence of CIVs, their causes,
> impact, and the complexity to address them. We stress the
> critical importance of CIVs in compartmentalization approaches,
> demonstrating an attack to extract isolated keys in OpenSSL and
> uncovering a decade-old vulnerability in sudo. We show, among
> others, that not all interfaces are affected in the same way, that
> API size is uncorrelated with CIV prevalence, and that addressing
> interface vulnerabilities goes beyond writing simple checks. We
> conclude the paper with guidelines for CIV-aware compartment
> interface design, and appeal for more research towards systematic
> CIV detection and mitigation.

Disclaimer: like any research project, this is highly work in progress PoC.
Here be dragons! Happy hacking!

If at all possible, please read through this entire document before installing
or using ConfFuzz. This document is best read on
[GitHub](https://github.com/conffuzz/conffuzz), with a Markdown viewer, or
Markdown editor.

## 0. Table of Contents & Links

- [1. ConfFuzz High-Level Information](#1-conffuzz-high-level-information)
- [2. Setting Up ConfFuzz](#2-setting-up-conffuzz)
- [3. Running ConfFuzz](#3-running-conffuzz)
- [4. Example Targets](#4-example-targets)
- [5. Interpreting Crash Data](#5-interpreting-crash-data)
- [6. ConfFuzz Internals & Dev Tips](#6-conffuzz-internals--dev-tips)
- [7. Known Issues](#7-known-issues)
- [8. Pin Overhead Measurements](#8-pin-overhead-measurements)

**Link to the ConfFuzz NDSS paper data set: [[NDSS Data Set]](https://github.com/conffuzz/conffuzz-ndss-data)**

## 1. ConfFuzz High-Level Information

ConfFuzz is an in-memory fuzzer aimed at detecting interface vulnerabilities in
compartmentalized contexts. ConfFuzz supports sandbox scenarios (where an
untrusted component is isolated from the rest of application containment
purposes), as well as safebox scenarios (where a trusted, critical component is
isolated from the application to protect it). It instruments arbitrary APIs and
performs attacks through the API to trigger bugs in the trusted compartment.

ConfFuzz supports any granularity of components; libraries, modules, or any
arbitrary function-level API.

Vulnerability types supported (as of the current version of this document):

| Attack class     | Attack type                                         | Supported           |
|------------------|-----------------------------------------------------|---------------------|
| Information Leak | Exposure of Addresses                               | :hammer_and_wrench: |
|                  | Exposure of Compartment-confidential Data           | :x:                 |
|                  | Control-Dependent Data                              | :white_check_mark:  |
| Spatial          | Dereference of Corrupted Pointer                    | :white_check_mark:  |
|                  | Usage of Corrupted Indexing Information             | :white_check_mark:  |
|                  | Usage of Corrupted Object                           | :white_check_mark:  |
| Temporal         | Expectation of API Usage Ordering                   | :heavy_plus_sign:   |
|                  | Usage of Corrupted Synchronization Primitive        | :white_check_mark:  |
|                  | Shared memory TOCTTOU                               | :x:                 |

Legend:
- Supported: :white_check_mark:
- Partial: :heavy_plus_sign:
- Planned: :hammer_and_wrench:
- Unsupported: :x:

## 2. Setting Up ConfFuzz

### Using Docker

A recent, functional Docker installation is required for ConfFuzz development.

Setup and open a shell to a ready-to-use ConfFuzz development environment:
```
$ make devshell
```

The development environment has a copy of this directory in `/root`.

In the development environment, build ConfFuzz:
```
$ make conffuzz
```

Note that running `make` only will fail as it will try to build the example
docker containers, see below.

Finally, run it on an example application to sanity check your install:
```
$ make -C examples/app-example run-instrumented
```

### Without Docker

We highly recommend running ConfFuzz in Docker containers.

That being said, you can also run ConfFuzz on your own system, see the
Dockerfile (`conffuzz-dev.dockerfile`) for more precise instructions.

A few remarks:

- This code assumes a working C++17 installation, the code
  will not compile if your install is too old.
- This code depends on the LLVM DWARF dump tools. We are using the
  `llvm-11` package in the container images. If you cannot use LLVM
  11, for example because your distributation only provides a more recent
  version, you simply need to edit the two references to LLVM 11
  in `conffuzz/analyze-types.sh` and `conffuzz/analyze-symbols.sh`,
  e.g., replacing to `llvm-dwarfdump-11` to `llvm-dwarfdump-14` if
  you have LLVM 14.

## 3. Running ConfFuzz

The help message is self explanatory:

```
$ ./conffuzz/conffuzz
  _____          _______          
 / ___/__  ___  / _/ __/_ ________
/ /__/ _ \/ _ \/ _/ _// // /_ /_ /
\___/\___/_//_/_//_/  \_,_//__/__/

Usage: ./conffuzz/conffuzz [<opt. params>] -l <num libs> <target shared libs> \
                                              <app binary> -- [<app params>]

Optional application-specific parameters:
      -t <workload program binary> : workload generator for the application
      -r <api regex> : regex describing the component fuzz target's API
      -T <timeout> : max time between two interface crossings (default 30s)
      -x : (temporary) use version 2 of the symbol extracter (default 0)

Optional automation parameters:
      -s <seed> : RNG generator seed to use (default random)
      -i <iterations> : limit number of fuzzing iterations (default unlimited)
      -F <api file> : provide API description manually instead of generating it
      -G <types file> : provide types description manually instead of generating it
      -X : generate API and type description files, then exit (incompatible w/ -F and -G)
      -L <shared lib> : additional libraries to be used as part of type analysis
                        (-L can be passed multiple times to specify several libraries)

Optional output parameters:
      -O <crash path> : path to store fuzzer output (default /home/hle/Development/conffuzz/conffuzz/../)
      -d : enable debugging output
      -S : statically determine API entry point count
      -m : reproduce/minimize false positives (default 0)
      -D : enable heavy debugging mode
      -C : disable fancy output

Optional mode parameters:
 default : sandbox mode        (the application is attacked)
      -R : enable safebox mode (the component fuzz target is attacked)

Example:
    conffuzz -d -l 1 /lib/libgs.so.9.55 /usr/bin/convert -- foo.ps foo.pdf
    conffuzz -d /lib/libgs.so.9.55 /usr/bin/convert -- foo.ps foo.pdf
        (if -l is omitted, -l 1 is assumed)
```

### Terminology

With **component fuzz target**, we mean the component behind the fuzz-targeted
API. In a ImageMagick/libgs scenario, libgs is the component fuzz target. In a
Nginx/libssl scenario, libssl is the component fuzz target.

As said previously, component fuzz targets can be at any arbitrary
function-level granularity.

### Things you need to know

Please, **read this carefully**, along with the [troubleshooting section](https://github.com/conffuzz/conffuzz#known-issues).

Regarding the format of the input and the definition of the fuzz target API:

 - The component fuzz target (or a superset of it) must be available as a shared
   object (`.so`). ConfFuzz reads its symbol table to automatically determine the
   interface to instrument.
 - If the interface of the fuzz target component that you are targeting is too big, or if you do
   not want to target all of it (e.g., if you want to go at a deeper or finer API),
   you can pass a regex via `-r` to match the symbols that you want to target.
 - If you want to fuzz a library API: to reduce the size of the interface that
   ConfFuzz has to consider, try to build your library to expose only its public API
   symbols. You will often be able to do that with build system arguments like
   `--enable-hidden-visibility` or `-fvisibility=hidden`.

Regarding how you should compile fuzz targets:

 - Target applications AND component fuzz targets must be compiled
   with debugging enabled (`-g`).
 - The attacked entity (the application in the default sandbox mode, the
   component fuzz target in the optional safebox mode) must be compiled
   with ASan (`-fsanitize=address`) enabled.

Regarding limitations of ConfFuzz and things you should pay attention to in fuzz targets:

 - ConfFuzz does not support multiprocess applications (not a fundamental limitation,
   we will hopefully support it at some point with more engineering)
 - Make sure that applications do not define a handle for SIGSEGV, this conflicts
   with ASan.
 - The application must not manipulate file descriptors that it doesn't own, e.g., closing
   all file descriptors for the process. This will break ConfFuzz. More [here](https://github.com/conffuzz/conffuzz#known-issues).

Regarding other practical things:

 - Between each run, backup and remove your `crashes` directory. The fuzzer
   will refuse to run if you don't do so.

## 4. Example Targets

This repository contains a number of applications that has been fuzzed with ConfFuzz.

All containers can be built with:
```
$ make examples
```

Summary of applications fuzzed:

| Application                      | APIs (Sandbox model)                  | APIs (Safebox model)             |
|----------------------------------|---------------------------------------|----------------------------------|
| Okular (PDF reader)              | libpoppler, libmarkdown               |                                  |
| ImageMagick (Image processing)   | libghostscript, libpng, libtiff       |                                  |
| Apache HTTPD (web server)        | libmarkdown, mod_markdown		   |                                  |
| Nginx (web server)               | libpcre, mod_geoip			   | libssl, [ERIM-style key isolation](https://github.com/vahldiek/erim/blob/master/src/openssl/erimized/crypto/evp/e_aes.c) |
| lighttpd (web server)		   | mod_deflate			   |			              |
| bzip2 (compression)              | libbz2                                |                                  |
| file (system utility)            | libmagic                              |                                  |
| ffmpeg (video processing)        | libavcodec, libavfilter, libavformat  |                                  |
| curl (web client)                | libnghttp2                            | libssl                           |
| inkscape (image processing)      | libpoppler, libpng                    |                                  |
| git (version control)            | libcurl, libzlib, libpcre             |                                  |
| Wireshark (packet analyzer)      | libpcap, libzlib                      |                                  |
| GPA (GUI for GNUPG)	           |                                       | libpgpme                         |
| Redis (Key Value Store)          | mod_redisearch, mod_redisbloom	   |			              |
| exif (image processing)	   | libexif				   |			              |
| rsync (file copying tool)	   | libpopt				   |			              |
| Aspell (spell checker)           | libaspell	                           |			              |
| sudo (admin utility)		   | mod_sudoers			   | libapparmor, [sudo auth API](https://github.com/sudo-project/sudo/blob/main/plugins/sudoers/auth/API) |
| ssh (remote login tool)	   |					   | libcrypto			      |
| bind9 (DNS tool suite)	   | libxml2				   |				      |
| memcached (key-value store)	   |					   | libsasl			      |
| haproxy (load balancer + proxy)  | libslz (internal)			   |				      |
| squid (reverse proxy)		   | libxml2, libexpat			   |				      |
| GPG (crypto software)		   |					   | libgcrypt			      |
| su (switch user utility)	   | libaudit				   | 				      |

Summary of applications work in progress:

| Application                      | APIs (Sandbox model)		   | APIs (Safebox model)      |
|----------------------------------|---------------------------------------|---------------------------|
| Firefox (wip)                    | libwoff2				   |			       |

#### Okular (isolated libpoppler, libmarkdown)

Open a shell in a custom development environment with:
```
$ make okularshell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### libpoppler

Start the fuzzer with:
```
$ QT_DEBUG_PLUGINS=1 QT_QPA_PLATFORM=xcb ./conffuzz/conffuzz -d -F api/libpoppler/functions.txt -G api/libpoppler/types.txt -l2 -T60 -r '^(?=.*Poppler)(((?!(Private|Annotation|Sound|Movie|FormField)).)*)$' /usr/local/lib/libpoppler.so /usr/local/lib/libpoppler-qt5.so /root/okular/build/bin/okular -- foo.pdf
```

...or:

Start the fuzzer with:
```
$ QT_DEBUG_PLUGINS=1 QT_QPA_PLATFORM=xcb ./conffuzz/conffuzz -d -r "load" -l2 -T60 /usr/local/lib/x86_64-linux-gnu/libpoppler.so /usr/local/lib/x86_64-linux-gnu/libpoppler-qt5.so /root/okular/build/bin/okular -- foo.pdf
```

to target one particular interesting function.

Note here: libpoppler is separated in two parts, the backend (libpoppler.so) and the frontend (libpoppled-qt5/glib/etc.so). Consider both as a single logical entity and fuzz simultaneously with `-l2`.

##### libmarkdown

Start the fuzzer with:
```
$ QT_DEBUG_PLUGINS=1 QT_QPA_PLATFORM=xcb ./conffuzz/conffuzz -d -F api/libmarkdown/functions.txt -G api/libmarkdown/types.txt -T60 /usr/lib/libmarkdown.so /root/okular/build/bin/okular -- foo.md
```

#### ImageMagick (isolated libghostscript, libpng, libtiff)

Open a shell in a custom development environment with:
```
$ make magickshell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### libghostscript

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -F api/libgs/functions.txt -G api/libgs/types.txt -r "gsapi" -d -T60 ghostscript-9.55.0/sodebugbin/libgs.so.9.55 /usr/local/bin/convert -- foo.ps foo.pdf
```

##### libpng

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -F api/libpng/functions.txt -G api/libpng/types.txt -d -T60 /usr/local/lib/libpng16.so /usr/local/bin/convert -- foo.ps foo.png
```

##### libtiff

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -F api/libtiff/functions.txt -G api/libtiff/types.txt -d -T60 /usr/local/lib/libtiff.so /usr/local/bin/convert -- foo.ps foo.tiff
```

#### Apache HTTPD (isolated libmarkdown, mod\_markdown)

Open a shell in a custom development environment with:
```
$ make apacheshell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### mod\_markdown

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -F api/apache-mod-markdown/functions.txt -G api/apache-mod-markdown/types.txt -d /usr/local/apache2/modules/mod_markdown.so -L /root/discount-2.2.7/libmarkdown.so.2.2.7 -L ./httpd-2.4.52/httpd ./httpd-2.4.52/httpd -t ./examples/apachescript.sh -- -X
```

##### libmarkdown

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -F api/apache-libmarkdown/functions.txt -G api/apache-libmarkdown/types.txt -d /root/discount-2.2.7/libmarkdown.so.2.2.7 ./httpd-2.4.52/httpd -t ./examples/apachescript.sh -- -X
```

...or:
```
$ ./conffuzz/conffuzz -F api/apache-libmarkdown/functions.txt -G api/apache-libmarkdown/types.txt -d /root/discount-2.2.7/libmarkdown.so.2.2.7 ./httpd-2.4.52/httpd -r "mkd_document" -t ./examples/apachescript.sh -- -X
```

to target one particular interesting function.

##### Known issues with Apache

- We did not manage to completely disable multiprocess. Because of this, on certain slow machines, the fuzzer does not manage to correctly fuzz Apache. This is a `FIXME`.

#### Redis (isolated mod_redisearch, mod_redisbloom)

Open a shell in a custom development environment with:
```
$ make redisshell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### mod\_redisbloom

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -m -d /root/RedisBloom/redisbloom.so -L /root/redis-6.2.6/src/redis-server /root/redis-6.2.6/src/redis-server -r '(RedisCommand|_OnLoad)$' -t ./examples/redis-redisbloom.sh -- /root/redis-6.2.6/redis.conf --loadmodule /root/RedisBloom/redisbloom.so
```

##### mod\_redisearch

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -m -d /root/RediSearch/bin/linux-x64-debug/search/redisearch.so -L /root/redis-6.2.6/src/redis-server /root/redis-6.2.6/src/redis-server -r '^(?=[A-Z]([a-zA-Z]+)(Command|_OnLoad)$)(((?!ACL).)*)$' -t ./examples/redis-redisearch.sh -- /root/redis-6.2.6/redis.conf --loadmodule /root/RediSearch/bin/linux-x64-debug/search/redisearch.so
```

#### Nginx (isolated libpcre, GeoIP module and safebox libssl, erim-style key isolation)

Open a shell in a custom development environment with:
```
$ make nginxshell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### libpcre

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -d /usr/local/lib/libpcre2-8.so.0.10.4 ./nginx-1.21.6/objs/nginx -F api/libpcre2/functions.txt -G api/libpcre2/types.txt -t examples/nginxscript.sh -- -g 'daemon off; error_log stderr debug; master_process off;'
```

##### GeoIP module

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -d /usr/local/nginx/modules/ngx_http_geoip_module.so /root/nginx-1.21.6/objs/nginx -F api/modgeoip/functions.txt -G api/modgeoip/types.txt -t examples/nginxscript.sh -- -g 'load_module modules/ngx_http_geoip_module.so; daemon off; error_log stderr debug; master_process off;'
```

##### libssl (safebox)

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -R -d -l 2 /usr/local/lib/libssl.so.1.1 /usr/local/lib/libcrypto.so.1.1 -F api/libssl/functions.txt -G api/libssl/types.txt -t examples/nginxscript-ssl.sh ./nginx-1.21.6/objs/nginx -- -g 'daemon off; error_log stderr debug; master_process off;'
```

Note here: libssl is a "superset" of libcrypto, and so any crash in libcrypto can be considered as a libssl crash as well. Thus, instrument both libraries.

##### [*ERIM-style* key isolation](https://github.com/vahldiek/erim/blob/master/src/openssl/erimized/crypto/evp/e_aes.c) (safebox)

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -x -R -d /usr/local/lib/libcrypto.so.1.1 -F api/libssl-erim/functions.txt -G api/libssl-erim/types.txt -r '(AES|aesni|asm_AES)_?(ecb|set|cfb1|cbc)?_?(encrypt|decrypt)_?(key|intern|blocks)?' -t examples/nginxscript-ssl.sh ./nginx-1.21.6/objs/nginx  -- -g 'daemon off; error_log stderr debug; master_process off;'
```

Note the `-x`. This option is only temporary and allows us to reach non-exposed
symbols. Ultimately it should always be on.

#### lighttpd2 (isolated mod_deflate)

Open a shell in a custom development environment with:
```
$ make lighttpdshell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### mod_deflate

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -d -x -m -l 1 /usr/local/lib/mod_deflate.so -r '^mod_deflate.*' /root/lighttpd-1.4.67/src/lighttpd -t examples/lighttpdscript.sh -- -f lighttpd.conf -D
```

#### bzip2 (isolated libbz2)

Open a shell in a custom development environment with:
```
$ make bzip2shell
```

Build the fuzzer with:
```
$ make conffuzz
```

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -d /lib/x86_64-linux-gnu/libbz2.so.1 ./bzip2-1.0.8/bzip2-shared -- -f -k -d example.bz2
```

#### ssh (safebox libcrypto)

Open a shell in a custom development environment with:
```
$ make sshshell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### libcrypto

Prepare the system with:
```
$ service ssh start # to start a local openssh server
```

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -R -d /usr/local/lib/libcrypto.so /usr/local/bin/ssh -- -o StrictHostKeyChecking=no localhost ls /root
```

#### sudo (sandbox sudo module API, safebox libapparmor and sudo auth API)

Switch to the branch `hlefeuvre/preload-crypt`:
```
git checkout hlefeuvre/preload-crypt
```

This is necessary to circumvent a bug in sudo/libcrypt.

Open a shell in a custom development environment with:
```
$ make sudoshell
```

Build the fuzzer with:
```
$ make conffuzz
```

Prepare the system with:
```
$ echo "x" | ASAN_OPTIONS=detect_leaks=0 LD_PRELOAD=/lib/x86_64-linux-gnu/libcrypt.so.1.1.0 sudo -S man
```

This command caches the root password to avoid the prompt later (this would
cause problems in the ConfFuzz fuzzing phase).

##### libapparmor

Prepare the system with:
```
$ ./start-apparmor.sh # enable & start apparmor in the container
```

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -R -d /usr/local/lib/libapparmor.so /usr/local/bin/sudo -- man
```

##### [sudo auth API](https://github.com/sudo-project/sudo/blob/main/plugins/sudoers/auth/API)

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -R -d -F api/sudo-authapi/functions.txt -G api/sudo-authapi/types.txt /usr/local/libexec/sudo/sudoers.so /usr/local/bin/sudo -- man
```

##### [sudo Module API](https://www.sudo.ws/docs/man/1.8.16/sudo_plugin.man/)

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -d -F api/sudo-modsudoers/functions.txt -G api/sudo-modsudoers/types.txt /usr/local/libexec/sudo/sudoers.so /usr/local/bin/sudo -- man
```

#### file (isolated libmagic)

Open a shell in a custom development environment with:
```
$ make fileshell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### libmagic

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -d /usr/lib/x86_64-linux-gnu/libmagic.so.1 /usr/local/bin/file -- README.md /usr/lib/x86_64-linux-gnu/libmagic.so.1 /usr/local/bin/file
```

#### ffmpeg (isolated libavcodec, libavfilter, libavformat)

Open a shell in a custom development environment with:
```
$ make ffmpegshell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### libavcodec

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -T60 -r "avcodec_" -d /usr/local/lib/libavcodec.so /root/ffmpeg-5.0/ffprobe -- foo.avi
```

##### libavfilter

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -T60 -r "avfilter_" -d /usr/local/lib/libavfilter.so /root/ffmpeg-5.0/ffprobe -- foo.avi
```

##### libavformat

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -T60 -r "avformat_" -d /usr/local/lib/libavformat.so /root/ffmpeg-5.0/ffprobe -- foo.avi
```

#### curl (isolated libnghttp2, and safebox libssl)

Open a shell in a custom development environment with:
```
$ make curlshell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### libnghttp2

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -d /usr/local/lib/libnghttp2.so.14 /usr/local/bin/curl -- --http2 https://google.com
```

##### libssl (safebox)

```
$ ./conffuzz/conffuzz -R -d -l 2 /usr/local/lib/libssl.so.1.1 /usr/local/lib/libcrypto.so.1.1 /usr/local/bin/curl -- https://google.com
```

See [Nginx/libssl](https://github.com/conffuzz/conffuzz#libssl-safebox) for more explanations.

#### inkscape (isolated libpoppler, libpng)

Open a shell in a custom development environment with:
```
$ make inkscapeshell
```

Build the fuzzer with:
```
$ make conffuzz
```

#### libpoppler

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -r "poppler_" -d -l2 /usr/local/lib/libpoppler-glib.so.8 /usr/local/lib/libpoppler.so /root/inkscape-INKSCAPE_1_1_2/build/bin/inkscape -- --pdf-poppler -o foo.svg foo.pdf
```

##### libpng

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -T60 -d /usr/local/lib/libpng16.so /root/inkscape-INKSCAPE_1_1_2/build/bin/inkscape -- -o foo.png foo.pdf
```

#### git (isolated libcurl, libzlib, libpcre)

Open a shell in a custom development environment with:
```
$ make gitshell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### libpcre

Start the fuzzer with:
```
$ cd repo-example
$ /root/conffuzz/conffuzz -d -F /root/api/git-libpcre/functions.txt -G /root/api/git-libpcre/types.txt /usr/local/lib/libpcre2-8.so.0 /root/git-2.35.1/git-grep -- -F "tcl"
```

##### libcurl

Start the fuzzer with:
```
$ cd repo-example
# finds a few bugs
$ /root/conffuzz/conffuzz -d -F /root/api/git-libcurl/functions.txt -G /root/api/git-libcurl/types.txt /usr/local/lib/libcurl.so.4 /root/git-2.35.1/git-http-push -- https://github.com/conffuzz/sqlite-splitsrc.git
# does not find bugs
$ /root/conffuzz/conffuzz -d -F /root/api/git-libcurl/functions.txt -G /root/api/git-libcurl/types.txt /usr/local/lib/libcurl.so.4 /root/git-2.35.1/git-http-fetch -- 567bbb2b91b742d6bbc15f275cf132813bfb7ff6 https://github.com/conffuzz/sqlite-splitsrc.git
```

##### libzlib

Start the fuzzer with:
```
$ cd repo-example
# does not find bugs
$ /root/conffuzz/conffuzz -d /usr/local/lib/libz.so.1 /root/git-2.35.1/git-http-fetch -- 567bbb2b91b742d6bbc15f275cf132813bfb7ff6 https://github.com/conffuzz/sqlite-splitsrc.git
```

#### wireshark (isolated libpcap, libzlib)

Open a shell in a custom development environment with:
```
$ make wiresharkshell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### libpcap

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -d /usr/local/lib/libpcap.so.1.10.1 -t examples/wiresharkscript.sh /root/wireshark_build/run/dumpcap
```

##### libzlib

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -d /usr/local/lib/libz.so.1.2.12 /root/wireshark_build/run/tshark -- -r /root/wireshark-3.4.12/test/captures/grpc_stream_reassembly_sample.pcapng.gz
```

#### GPA (isolated safebox libgpgme)

Open a shell in a custom development environment with:
```
$ make gpashell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### libgpgme (safebox)

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -d -R -T60 /usr/local/lib/x86_64-linux-gnu/libgpgme.so.11 /root/gpa-0.10.0/src/gpa -- -k
```

#### GPG (safebox libgcrypt)

Open a shell in a custom development environment with:
```
$ make gpashell
```

Note that this is not a typo, we reuse the GPA shell here.

Build the fuzzer with:
```
$ make conffuzz
```

##### libgcrypt (safebox)

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -R /usr/local/lib/x86_64-linux-gnu/libgcrypt.so.20.3.2 -r "^gcry_.*" -d /usr/local/bin/gpg -- --batch --gen-key /root/ndss-example.batch
```

#### Firefox (isolated libwoff2)

Open a shell in a custom development environment with:
```
$ make firefoxshell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### libwoff2

Start the fuzzer with:
```
$ MOZ_FORCE_DISABLE_E10S=1 ./conffuzz/conffuzz -d ./mozilla-unified/obj-x86_64-pc-linux-gnu/modules/woff2/libwoff2.so ./mozilla-unified/obj-x86_64-pc-linux-gnu/dist/bin/firefox
```

#### exif (isolated libexif)

Open a shell in a custom development environment with:
```
$ make exifshell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### libexif

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -d /usr/local/lib/libexif.so.12 /usr/local/bin/exif -- -l -e -d --show-mnote /root/exif_sample.jpg
```

#### aspell (isolated libaspell)

Open a shell in a custom development environment with:
```
$ make aspellshell
```

Build the fuzzer with:
```
$ make conffuzz-dbg
```

##### libaspell

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -d /usr/local/lib/libaspell.so.15 /usr/local/bin/aspell -t ./examples/aspellscript.sh
```

#### Memcached (isolated libsasl2)

Open a shell in a custom development environment with:
```
$ make memcachedshell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### libsasl2

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -R -d /usr/local/lib/libsasl2.so.3 -t examples/memcachedscript.sh ./memcached-1.6.17/memcached  --  -l 127.0.0.1 -p 11211 -m 64 -S -vvv -u root
```

##### Internal assoc API (hashmap)

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -x -m  /root/memcached-1.6.17/memcached -r "assoc_*"  -D -t examples/memcachedscript.sh ./memcached-1.6.17/memcached -- -l 127.0.0.1 -p 11211 -m 64 -vvv -u root
```

#### rsync (isolated libpopt)

Open a shell in a custom development environment with:
```
$ make rsyncshell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### libpopt

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -d /usr/local/lib/libpopt.so.0.0.2 /root/rsync-3.2.7/rsync -- --daemon -g /root/README.md /root/NDSS
```

#### Bind9 (isolated libxml2)

Open a shell in a custom development environment with:
```
$ make bind9shell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### libxml2

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -d /usr/local/lib/libxml2.so.2 -t examples/bindscript.sh ./bind-9.18.8/bin/named/.libs/named -- -g
```

#### squid (isolated libxml2)

Switch to the branch `hlefeuvre/disable-new-delete-mismatch`:
```
git checkout hlefeuvre/disable-new-delete-mismatch
```

This is necessary to circumvent a bug in squid.

Open a shell in a custom development environment with:
```
$ make squidshell
```

Then start Nginx (squid is just a proxy):
```
$ service nginx start
```

Build the fuzzer with:
```
$ make conffuzz
```

##### libxml2

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -d /usr/local/lib/libxml2.so.2.10.3 -F api/squid-libxml2/functions.txt -G api/squid-libxml2/types.txt -t /root/examples/squidscript.sh /usr/local/squid/sbin/squid -- -C -N -f /usr/local/squid/etc/squid.conf
```

##### libexpat

Update the configuration to use libexpat:
```
sed -i "s/libxml2/libexpat/g" /usr/local/squid/etc/squid.conf
```

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -d /usr/local/lib/libexpat.so.1.8.10 -F api/squid-libexpat/functions.txt -G api/squid-libexpat/types.txt -t /root/examples/squidscript.sh /usr/local/squid/sbin/squid -- -C -N -f /usr/local/squid/etc/squid.conf
```

#### su (isolated libaudit)

Open a shell in a custom development environment with:
```
$ make sushell
```

Build the fuzzer with:
```
$ make conffuzz
```

##### libaudit

Start the fuzzer with:
```
$ ./conffuzz/conffuzz -d /usr/local/lib/libaudit.so.1 -F api/su-libaudit/functions.txt -G api/su-libaudit/types.txt /root/shadow-4.13/src/su -- -c 'echo "hello"' root
```

## 5. Interpreting Crash Data

After running ConfFuzz for a while, interrupt it with CTRL-C.

Provided ConfFuzz detected some crashes, the `crashes` directory should look roughly like that:

```
crashes/
|-- bugs
|   `-- crash0
|       |-- crash_info.txt
|       |-- crash_trace.txt
|       |-- minimal
|       |   |-- app.log
|       |   |-- input.log
|       |   `-- mappings.txt
|       |-- run1
|       |   |-- app.log
|       |   |-- input.log
|       |   `-- mappings.txt
|       |-- run2
|       |   |-- app.log
|       |   |-- input.log
|       |   `-- mappings.txt
|       |-- run3
|       |   |-- app.log
|       |   |-- input.log
|       |   `-- mappings.txt
|       `-- run5
|           |-- app.log
|           |-- input.log
|           `-- mappings.txt
|-- bugs-non-ASan
|   `-- crash2
|       |-- crash_trace.txt
|       `-- run6
|           |-- app.log
|           |-- input.log
|           `-- mappings.txt
|-- false-positives
|   `-- crash1
|       |-- crash_trace.txt
|       `-- run4
|           |-- app.log
|           |-- input.log
|           `-- mappings.txt
|-- instrumented_functions.txt
`-- session_info.txt
```

- `session_info.txt` contains generic information about the fuzzing session.
  Start time, end time, seed used, but also size of the instrumented API,
  number of endpoints reached, etc.
- `instrumented_functions.txt` contains a precise list of the API instrumented
  and fuzzed. The number of entries should match "Total instrumented API size"
  in `session_info.txt`.
- Crashes are mainly segregated between "bugs" and "false positives". *Bugs*
  are crashes that happen in the trusted component due to the malicious output
  of the untrusted component. *False positives* are crashes *in the untrusted
  component* that happen because the untrusted component's malicious output is
  fed to itself by the trusted one. Return to sender.
- Crashes are *deduplicated* based on stack traces. That is, one crash may
  correspond to multiple runs that triggered the same bug. Each run has its own
  application logs and fuzzer logs (what the fuzzer did), and a copy of the
  application's AS mappings for debugging. Runs are stored under
  `run$(run number)` directories.
- There is only one `crash_trace.txt` file per crash. This is the "normalized" stack
  trace that all runs in a crash share. Recall that two runs are considered
  duplicates, if, after removing addresses from the stack trace, the trace is the
  same. This is the stack trace the fuzzer uses internally for deduplication.
- There is only one `crash_info.txt` file per crash. This file contains the result of the
  fuzzer's automated analysis of the crash (location of the fault, impact, etc.).
  Non-ASan crashes don't get analyzed.
- When the fuzzer finds a new bug (that is, one that it has never seen before),
  it analyzes its crash output and tries to reproduce it. If the bug was not
  detected by ASan (might happen unfortunately, it is then a simple SIGSEGV), the
  bug will be classified differently and put under `bugs-non-ASan`. If the bug
  cannot be reproduced (might happen for a number of reasons, including
  in-application randomization, or a very particular application state), the bug
  will not be minimized and will be tagged `non-reproducible` in `crash_info.txt`.
  These bugs **are still valid**, they will just be harder to debug/interpret.
- Finally, when the fuzzer finds a new bug, it minimizes it to obtain the
  minimal set of changes that trigger the bug. Each bug has its minimal
  reproducer under `minimal/`.

## 6. ConfFuzz Internals & Dev Tips

ConfFuzz uses Intel Pin to instrument shared libraries. The monitor and workers
communicate using FIFOs.

Another document, [STRUCTURE.md](https://github.com/conffuzz/conffuzz/blob/main/STRUCTURE.md),
contains information about the structure of this repository. You might want to read
it if you are planning to extend ConfFuzz.

### Building ConfFuzz for development

You can build ConfFuzz for debugging with:
```
$ make conffuzz-dbg
```

### Avoiding long API extraction times

In case of big APIs, extracting the signatures and types can take a while:

```
[+] Retrieving library symbols (can take a bit of time)
(...)
[+] Retrieving symbol type information (can take a bit of time)
```

Once you have done this once, you can reuse the API file
(`/tmp/conffuzz_functions.txt`) by passing it to the fuzzer as `-F`, and the
types file (`/tmp/conffuzz_types.txt`) by passing it to the fuzzer as `-G`.

We are already doing this for all scenarios under `examples`, see rules
`genapi-$app` in the Makefile.

**Important note**: DO NOT do `-F /tmp/conffuzz_functions.txt`, this DOES NOT
work. Instead, do a backup manually, e.g., `/tmp/conffuzz_functions.backup` and
then do `-F /tmp/conffuzz_functions.backup`. Same goes for types files.

## 7. Known issues

- ASan reports are not as good with Intel Pin. Intel Pin seems to annoy ASan but
  there is not much we can do about that.

- The following error pops while retrieving library symbols from time to time. It is
  benign, you can safely ignore it.
```
[+] Retrieving library symbols (can take a bit of time)
error: decoding address ranges: invalid range list offset 0x857
```

- Sometimes, building containers might fail with an error like:
```
Get:60 http://deb.debian.org/debian bookworm/main amd64 libxslt1.1 amd64 1.1.34-4 [239 kB]
Get:61 http://deb.debian.org/debian bookworm/main amd64 libxslt1-dev amd64 1.1.34-4 [329 kB]
Get:62 http://deb.debian.org/debian bookworm/main amd64 quilt all 0.66-2.1 [319 kB]
E: Failed to fetch http://deb.debian.org/debian/pool/main/libx/libx11/libx11-dev_1.7.2-2%2bb1_amd64.deb  404  Not Found [IP: 199.232.54.132 80]
E: Unable to fetch some archives, maybe run apt-get update or try with --fix-missing?
E: Failed to process build dependencies
```
  In this case, simply rebuild the full container with `--no-cache=true`, e.g.:
```
$ docker build --no-cache=true -t conffuzz-nginx -f examples/nginx.dockerfile .
```

- The fuzzer should not be moved out of its working directory, otherwise you will get errors like:
```
[E] Could not find instrumentation, has this binary been moved?
```
  This is because the fuzzer works as a bundle (the monitor, the instrumentation, the
  various binary analysis scripts), and has some hardcoded paths relative to its position. If you
  want to copy conffuzz to another setup, copy the entire conffuzz directory.

- If one of the previous commands fails with something like this:
```
[E] Passed shared library path looks invalid.
[E] There is nothing here: "/usr/local/lib/x86_64-linux-gnu/libpoppler.so"
```
  Try to locate the library using:
```
# ldconfig -p | grep libpoppler.so
	libpoppler.so.120 (libc6,x86-64) => /usr/local/lib/libpoppler.so.120
	libpoppler.so (libc6,x86-64) => /usr/local/lib/libpoppler.so
```
  It might have changed location with a rebuild.

- The fuzzer outputs **instrumentation bug** in this message:
```
[+] Death of worker $pid detected (exited, code 66 = instrumentation bug)
```
  Or this, if `-D` is passed:
```
[+] Death of worker $pid detected (exited, code 66 = instrumentation bug)
[E] {conffuzz.cpp:$loc} Instrumentation bug detected, aborting.
```
  In this case, check two things **very** carefully: 1) is the target app forking?
  And 2), is the target app closing all file descriptors? If yes, this is the source
  of this problem. Look at ssh for an example where [the latter was an issue](https://github.com/conffuzz/conffuzz/blob/main/examples/ssh.dockerfile).

  If no, that's a bug in ConfFuzz! Please report it!

## 8. Pin Overhead Measurements

We have implemented a few benchmarks to evaluate the cost of the Pin DBI
framework.

Methodology: we measure the cost of the Pin framework. We run the application
instrumented *without fuzzing* and observe the overhead. The overhead observed
is exclusively due to Pin.

Numbers presented here were run on an Intel(R) Core(TM) i7-10510U CPU @
1.80GHz.

### Nginx instrumentated at libpcre boundary

The `make pin-overhead-nginx` rule runs the benchmark.

You should get something in the lines of:
```
$ make pin-overhead-nginx
(...)
Benchmarking Nginx instrumented (this will take a while)
8485.50
7519.75
7593.39
7668.09
7793.58
7772.78
7696.35
7721.33
7696.50
7774.82
7124.08
AVERAGE: 7713.2881818182
Benchmarking Nginx vanilla (this will take a while)
22462.90
22194.33
21642.16
21905.25
21966.39
21955.46
22037.30
20763.78
21866.37
22349.53
22574.47
AVERAGE: 21974.358181818
AVERAGE OVERHEAD: 65%
```

The overhead is rather low; few functions are instrumented and the
instrumentation is fast.

### Redis instrumentated at module boundary

The `make pin-overhead-redis` rule runs the benchmark.

You should get something in the lines of:
```
$ make pin-overhead-redis
(...)
Benchmarking Redis instrumented (this will take a while)
136054.42
147929.00
139470.02
141442.72
137174.22
140646.97
141665.73
147710.48
130548.30
132978.73
136425.66
AVERAGE: 139276.93181818
Benchmarking Redis vanilla (this will take a while)
334769.25
294870.59
383877.38
383570.88
386100.38
356612.12
361761.72
301397.59
367647.03
301253.03
302163.16
AVERAGE: 343093.01181818
AVERAGE OVERHEAD: 60%
```

The overhead is very similar to Nginx.

### libxml2 tests instrumented at libxml2 boundary

In this case we can use Pin in probed mode to run natively (reasons: single
threaded, simple return values).

```
$ git checkout hlefeuvre/probedinstrumentation
$ make pin-overhead-libxml2
Benchmarking xmltest instrumented (this will take a while)
2.440
2.400
2.374
2.422
2.442
2.439
2.393
2.409
2.429
2.414
2.424
AVERAGE: 2.4169090909091
Benchmarking xmltest vanilla (this will take a while)
2.068
2.074
2.116
2.100
2.132
2.128
2.121
2.111
2.078
2.114
2.098
AVERAGE: 2.1036363636364
AVERAGE OVERHEAD: 15%
```

If we use the standard JIT Pin:

```
$ make pin-overhead-libxml2
Benchmarking xmltest instrumented (this will take a while)
10.822
10.831
10.776
11.161
11.970
12.696
11.957
12.086
12.225
12.013
12.429
AVERAGE: 11.724181818182
Benchmarking xmltest vanilla (this will take a while)
2.185
2.326
2.199
2.283
2.206
2.271
2.204
2.140
2.253
2.155
2.165
AVERAGE: 2.217
AVERAGE OVERHEAD: 5.2x
```

The entire 5x overhead is due to Pin's JIT. Fortunately we can avoid it in this
case and many other similar situations.

### file instrumentated at libmagic boundary

In this case we can use Pin in probed mode to run natively (same reasons as
xmltest).

The `make pin-overhead-file` rule runs the benchmark.

You should get something in the lines of:
```
$ git checkout hlefeuvre/probedinstrumentation
$ make pin-overhead-file
(...)
Benchmarking file instrumented (this will take a while)
0.091
0.091
0.106
0.129
0.101
0.093
0.147
0.099
0.128
0.123
0.133
AVERAGE: 0.11281818181818
Benchmarking file vanilla (this will take a while)
0.062
0.065
0.066
0.034
0.072
0.061
0.044
0.061
0.066
0.047
0.068
AVERAGE: 0.058727272727273
AVERAGE OVERHEAD: 48%
```

If we use the standard JIT Pin:

```
$ make pin-overhead-file
(...)
Benchmarking file instrumented (this will take a while)
1.267
1.217
1.223
1.213
1.206
1.219
1.220
1.210
1.217
1.207
1.210
AVERAGE: 1.219
Benchmarking file vanilla (this will take a while)
0.040
0.069
0.042
0.040
0.042
0.038
0.041
0.042
0.041
0.042
0.041
AVERAGE: 0.043
AVERAGE OVERHEAD: 2705%
```

The overhead is very high (27x) and entirely due to Pin's JIT, resulting in a
similar observation to xmltest.
