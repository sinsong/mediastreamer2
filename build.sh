mkdir download/
wget --directory-prefix=download/ `more download_depences.txt`

mkdir ../deps/
tar -xzf download/bctoolbox-0.6.0.tar.gz -C ../deps/
tar -xzf download/ortp-1.0.2.tar.gz -C ../deps/
tar -xzf download/bzrtp-1.0.6.tar.gz -C ../deps/
tar -xzf download/mbedtls-2.16.1.tar.gz -C ../deps/
tar -xzf download/bcunit-3.0.2.tar.gz -C ../deps/
tar -xzf download/sqlite-autoconf-3270200.tar.gz -C ../deps/
tar -xzf download/speex-1.2.0.tar.gz -C ../deps/
tar -xzf download/speexdsp-1.2rc3.tar.gz -C ../deps/
tar -xzf download/opus-1.3.tar.gz -C ../deps/

mypath=$(pwd)

echo build --- mbedtls
cd ../deps/mbedtls-mbedtls-2.16.1
cmake .
cmake --build . --target install
cd ${mypath}

echo build -- bcunit
cd ../deps/BCunit-3.0.2-Source
cmake .
cmake --build . --target install
cd ${mypath}

echo build --- bctoolbox
cd ../deps/bctoolbox-0.6.0
cmake -DENABLE_SHARED=OFF .
cmake --build . --target install
cd ${mypath}

echo build --- sqlite
sh -c "cd ../deps/sqlite-autoconf-3270200 && ./configure && make install"

echo build --- bzrtp
cd ../deps/bzrtp-1.0.6
cmake -DENABLE_SHARED=OFF .
cmake --build . --target install
cd ${mypath}

echo build --- ortp
cd ../deps/ortp-1.0.2-0
cmake -DENABLE_SHARED=OFF .
cmake --build . --target install
cd ${mypath}

echo build --- speex
sh -c "cd ../deps/speex-1.2.0 && ./configure && make install"

echo build --- speexdsp
sh -c "cd ../deps/speexdsp-1.2rc3 && ./configure && make install"

echo build --- opus
sh -c "cd ../deps/opus-1.3 && ./configure && make install"