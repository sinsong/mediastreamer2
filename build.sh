mkdir download/
wget --directory-prefix=download/ `more download_depences.txt`

mkdir deps/
tar -xzf download/bctoolbox-0.6.0.tar.gz -C deps/
tar -xzf download/ortp-1.0.2.tar.gz -C deps/
tar -xzf download/bzrtp-1.0.6.tar.gz -C deps/
tar -xzf download/mbedtls-2.16.1.tar.gz -C deps/
tar -xzf download/bcunit-3.0.2.tar.gz -C deps/
tar -xzf download/sqlite-autoconf-3270200.tar.gz -C deps/
tar -xzf download/speex-1.2.0.tar.gz -C deps/
tar -xzf download/speexdsp-1.2rc3.tar.gz -C deps/
tar -xzf download/opus-1.3.tar.gz -C deps/

mkdir b
mkdir b/mbedtls
echo build --- mbedtls
cmake -S deps/mbedtls-mbedtls-2.16.1 -B b/mbedtls
sh -c "cd b/mbedtls && make install"

mkdir b/bcunit
echo build -- bcunit
cmake -S deps/BCunit-3.0.2-Source -B b/bcunit
sh -c "cd b/bcunit && make install"

mkdir b/bctoolbox
echo build --- bctoolbox
cmake -DENABLE_SHARED=OFF -S deps/bctoolbox-0.6.0 -B b/bctoolbox
sh -c "cd b/bctoolbox && make install"

echo build --- sqlite
sh -c "cd deps/sqlite-autoconf-3270200 && ./configure && make install"

mkdir b/bzrtp
echo build --- bzrtp
cmake -DENABLE_SHARED=OFF -S deps/bzrtp-1.0.6 -D b/bzrtp
sh -c "cd b/bzrtp && make install"

mkdir b/ortp
cmake -DENABLE_SHARED=OFF -S deps/ortp-1.0.2-0 -B b/ortp
echo build --- ortp
sh -c "cd b/ortp && make install"

echo build --- speex
sh -c "cd deps/speex-1.2.0 && ./configure && make install"

echo build --- speexdsp
sh -c "cd deps/speexdsp-1.2rc3 && ./configure && make install"

echo build --- opus
sh -c "cd deps/opus-1.3 && ./configure && make install"