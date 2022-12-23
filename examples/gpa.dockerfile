#FROM conffuzz-dev:latest

FROM debian:11.5
 
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


WORKDIR /root


RUN apt -y update && apt -y install bzip2 wget2 make gettext texinfo doxygen gnutls-bin encfs fuse3 build-essential libbz2-dev zlib1g-dev libncurses5-dev libsqlite3-dev libldap2-dev libsecret-1-dev libgcr-3-dev libfltk1.3-dev libusb-1.0-0-dev policykit-1 autoconf
RUN apt -y install autopoint libtool m4

# Installing Libgpg-error 1.41
RUN wget2 -c https://gnupg.org/ftp/gcrypt/libgpg-error/libgpg-error-1.41.tar.bz2
RUN wget2 -c https://gnupg.org/ftp/gcrypt/libgpg-error/libgpg-error-1.41.tar.bz2.sig
RUN tar xjof libgpg-error-1.41.tar.bz2 && cd libgpg-error-1.41 && autoreconf -fi && CFLAGS="-g -O0" ./configure --sysconfdir=/etc --sharedstatedir=/var/lib --localstatedir=/var/local --runstatedir=/run --libdir=/usr/local/lib/x86_64-linux-gnu --enable-threads=posix --disable-rpath 
RUN cd libgpg-error-1.41 && make -j$(nproc)
RUN cd libgpg-error-1.41 && make install

# Installing Libgcrypt 1.9.2
RUN wget2 -c https://gnupg.org/ftp/gcrypt/libgcrypt/libgcrypt-1.9.2.tar.bz2
RUN wget2 -c https://gnupg.org/ftp/gcrypt/libgcrypt/libgcrypt-1.9.2.tar.bz2.sig
RUN tar xjof libgcrypt-1.9.2.tar.bz2
RUN cd libgcrypt-1.9.2 && autoreconf -fi && CFLAGS="-g -O0" ./configure --sysconfdir=/etc --sharedstatedir=/var/lib --localstatedir=/var/local --runstatedir=/run \
  --libdir=/usr/local/lib/x86_64-linux-gnu --enable-m-guard --enable-hmac-binary-check --with-capabilities && make -j$(nproc)
RUN cd libgcrypt-1.9.2 && make install

# Installing Libksba 1.5.0
RUN wget2 -c https://gnupg.org/ftp/gcrypt/libksba/libksba-1.5.0.tar.bz2
RUN wget2 -c https://gnupg.org/ftp/gcrypt/libksba/libksba-1.5.0.tar.bz2.sig
RUN tar xjof libksba-1.5.0.tar.bz2
RUN cd libksba-1.5.0 && autoreconf -fi && ./configure --sysconfdir=/etc --sharedstatedir=/var/lib --localstatedir=/var/local --runstatedir=/run \
  --libdir=/usr/local/lib/x86_64-linux-gnu && make -j$(nproc)
RUN cd libksba-1.5.0 &&  make check && make install

# Installing Libassuan 2.5.4
RUN wget2 -c https://gnupg.org/ftp/gcrypt/libassuan/libassuan-2.5.4.tar.bz2
RUN wget2 -c https://gnupg.org/ftp/gcrypt/libassuan/libassuan-2.5.4.tar.bz2.sig
RUN tar xjof libassuan-2.5.4.tar.bz2
RUN cd libassuan-2.5.4 && autoreconf -fi && CFLAGS="-g -O0" ./configure --sysconfdir=/etc --sharedstatedir=/var/lib --localstatedir=/var/local --runstatedir=/run \
  --libdir=/usr/local/lib/x86_64-linux-gnu && make -j$(nproc)
RUN cd libassuan-2.5.4 && make check -j$(nproc) && make install

# Installing nPth 1.6
RUN wget2 -c https://gnupg.org/ftp/gcrypt/npth/npth-1.6.tar.bz2
RUN wget2 -c https://gnupg.org/ftp/gcrypt/npth/npth-1.6.tar.bz2.sig
RUN tar xjof npth-1.6.tar.bz2
RUN cd npth-1.6 && autoreconf -fi && CFLAGS="-g -O0" ./configure --sysconfdir=/etc --sharedstatedir=/var/lib --localstatedir=/var/local \
  --runstatedir=/run --libdir=/usr/local/lib/x86_64-linux-gnu && make -j$(nproc)
RUN cd npth-1.6 && make check -j$(nproc) && make install

# Install GPGME 1.15.1
RUN wget2 -c https://gnupg.org/ftp/gcrypt/gpgme/gpgme-1.15.1.tar.bz2
RUN wget2 -c https://gnupg.org/ftp/gcrypt/gpgme/gpgme-1.15.1.tar.bz2.sig
RUN tar xjof gpgme-1.15.1.tar.bz2
RUN cd gpgme-1.15.1 && autoreconf -fi && CFLAGS="-g -O0" ./configure --sysconfdir=/etc --sharedstatedir=/var/lib --localstatedir=/var/local --runstatedir=/run \
  --libdir=/usr/local/lib/x86_64-linux-gnu && make -j$(nproc)
RUN cd gpgme-1.15.1 && make check -j$(nproc) && make install


# Install GnuPG 2.2.27
RUN wget2 -c https://gnupg.org/ftp/gcrypt/gnupg/gnupg-2.2.27.tar.bz2
RUN wget2 -c https://gnupg.org/ftp/gcrypt/gnupg/gnupg-2.2.27.tar.bz2.sig
RUN tar xjof gnupg-2.2.27.tar.bz2
RUN cd gnupg-2.2.27 && autoreconf -fi && CFLAGS="-g -O0" ./configure --sysconfdir=/etc --sharedstatedir=/var/lib --localstatedir=/var/local --runstatedir=/run \
  --libdir=/usr/local/lib/x86_64-linux-gnu --enable-g13 --enable-large-secmem --disable-rpath \
  --enable-run-gnupg-user-socket --with-capabilities
RUN sed -i "s/-O0/-O0 -fsanitize=address/g" /root/gnupg-2.2.27/g10/Makefile
RUN cd gnupg-2.2.27 && make -j$(nproc) && make install

# Install GPA 0.10.0
RUN apt install -y libgtk2.0-dev gpa libgtk2.0-0

RUN wget2 -c https://gnupg.org/ftp/gcrypt/gpa/gpa-0.10.0.tar.bz2
RUN wget2 -c https://gnupg.org/ftp/gcrypt/gpa/gpa-0.10.0.tar.bz2.sig
RUN tar xjof gpa-0.10.0.tar.bz2
RUN cd gpa-0.10.0 && autoupdate && autoreconf -fi 
RUN cd gpa-0.10.0 && ./configure --sysconfdir=/etc --sharedstatedir=/var/lib --localstatedir=/var/local --runstatedir=/run --libdir=/usr/local/lib/x86_64-linux-gnu --disable-rpath
RUN cd gpa-0.10.0 && make CFLAGS="-O0 -g -fsanitize=address" -j$(nproc) && make install

# batch file to fuzz gpg
RUN echo "     %echo Generating a basic OpenPGP key" >> /root/ndss-example.batch
RUN echo "     Key-Type: DSA" >> /root/ndss-example.batch
RUN echo "     Key-Length: 1024" >> /root/ndss-example.batch
RUN echo "     Subkey-Type: ELG-E" >> /root/ndss-example.batch
RUN echo "     Subkey-Length: 1024" >> /root/ndss-example.batch
RUN echo "     Name-Real: Joe Tester" >> /root/ndss-example.batch
RUN echo "     Name-Comment: with stupid passphrase" >> /root/ndss-example.batch
RUN echo "     Name-Email: joe@foo.bar" >> /root/ndss-example.batch
RUN echo "     Expire-Date: 0" >> /root/ndss-example.batch
RUN echo "     Passphrase: abc" >> /root/ndss-example.batch
RUN echo "     %pubring foo.pub" >> /root/ndss-example.batch
RUN echo "     %secring foo.sec" >> /root/ndss-example.batch
RUN echo "     # Do a commit here, so that we can later print "done" :-)" >> /root/ndss-example.batch
RUN echo "     %commit" >> /root/ndss-example.batch
RUN echo "     %echo done" >> /root/ndss-example.batch

ENV LD_LIBRARY_PATH="/lib:/usr/lib:/usr/local/lib"

WORKDIR /root
