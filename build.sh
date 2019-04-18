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
sh -c "cd b/mbedtls && cmake ../../deps/mbedtls-mbedtls-2.16.1"
cmake --build b/mbedtls --target install

mkdir b/bcunit
echo build -- bcunit
sh -c "cd b/bcunit && cmake ../../deps/BCunit-3.0.2-Source"
cmake --build b/bcunit --target install

mkdir b/bctoolbox
echo build --- bctoolbox
sh -c "cd b/bctoolbox && cmake -DENABLE_SHARED=OFF ../../deps/bctoolbox-0.6.0"
cmake --build b/bctoolbox --target install

echo build --- sqlite
sh -c "cd deps/sqlite-autoconf-3270200 && ./configure && make install"

mkdir b/bzrtp
echo build --- bzrtp
sh -c "cd b/bzrtp && cmake -DENABLE_SHARED=OFF ../../deps/bzrtp-1.0.6"
cmake --build b/bzrtp --target install

mkdir b/ortp
echo build --- ortp
sh -c "cd b/ortp && cmake -DENABLE_SHARED=OFF ../../deps/ortp-1.0.2-0"
cmake --build b/ortp --target install

echo build --- speex
sh -c "cd deps/speex-1.2.0 && ./configure && make install"

echo build --- speexdsp
sh -c "cd deps/speexdsp-1.2rc3 && ./configure && make install"

echo build --- opus
sh -c "cd deps/opus-1.3 && ./configure && make install"