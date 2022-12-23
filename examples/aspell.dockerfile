FROM conffuzz-dev:latest

RUN apt -y update
RUN apt install -y libtool gettext autopoint texinfo perl autoconf automake wget

# Clone and build aspell
RUN git clone https://github.com/GNUAspell/aspell.git
RUN cd aspell && ./autogen && ./configure CFLAGS="-g -fsanitize=address" CXXFLAGS="-g -fsanitize=address"
RUN cd aspell && make && make install

# Download en dictionary
RUN wget https://ftp.gnu.org/gnu/aspell/dict/en/aspell6-en-2020.12.07-0.tar.bz2
RUN tar -xf aspell6-en-2020.12.07-0.tar.bz2
RUN cd aspell6-en-2020.12.07-0 && ./configure CFLAGS="-g -fsanitize=address" && make && make install 

WORKDIR /root
