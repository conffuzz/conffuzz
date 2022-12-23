FROM conffuzz-dev:latest

RUN apt update
RUN apt install -y git build-essential
RUN apt install -y python3-distutils python3-apt
RUN apt install -y curl
RUN apt install -y python3-pip
RUN apt install -y mercurial
RUN apt install -y unzip zip
RUN curl https://sh.rustup.rs -sSf | sh -s -- -y

RUN curl https://hg.mozilla.org/mozilla-central/raw-file/default/python/mozboot/bin/bootstrap.py -O
RUN python3 bootstrap.py --no-interactive

RUN cd mozilla-unified/ && mkdir builds

COPY examples/firefox/Makefile mozilla-unified/builds/
COPY examples/firefox/mozconfig mozilla-unified/
COPY examples/firefox/moz.build mozilla-unified/modules/woff2

RUN apt update

RUN cd mozilla-unified/builds/ && make prepare
RUN cd mozilla-unified/builds/ && make build

WORKDIR /root
