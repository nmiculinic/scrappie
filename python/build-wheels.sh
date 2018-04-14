#!/bin/bash
set -e -x

# this is used in build.py
export MANYLINUX=1

BUILD_PREFIX="/usr/local"
OPENBLAS_VERS="0.2.18"
OPENBLAS_TAR=/io/python/openblas_${OPENBLAS_VERS}.tgz

function build_openblas {
    # this takes a long time so record success in openblas-built
    if [ -e "openblas-built" ]; then return; fi
    if [ -d "OpenBLAS" ]; then
        (cd OpenBLAS && git clean -fxd && git reset --hard)
    else
        git clone https://github.com/xianyi/OpenBLAS
    fi
    (cd OpenBLAS \
        && git checkout "v${OPENBLAS_VERSION}" \
        && make DYNAMIC_ARCH=1 USE_OPENMP=0 NUM_THREADS=64 TARGET=NEHALEM > /dev/null)
    touch openblas-built
}

function install_openblas {
    (cd OpenBLAS && make PREFIX=$BUILD_PREFIX install)
    tar zcf ${OPENBLAS_TAR} /usr/local/lib /usr/local/include
}


cd /io
# if there's a openblas tar lying around use it, else download,
# build, and install (making the tar along the way).
if [ -e ${OPENBLAS_TAR} ]; then
    tar xzf ${OPENBLAS_TAR} -C /
else
    build_openblas
    install_openblas
fi


cd /io/python
rm -rf wheelhouse

# compile wheels
for minor in 4 5 6; do
    PYBIN="/opt/python/cp3${minor}-cp3${minor}m/bin"
    "${PYBIN}/pip" wheel . -w wheelhouse/
done

for whl in wheelhouse/scrappy*.whl; do
    auditwheel repair "$whl" -w wheelhouse/
    rm "$whl"
done

# install and "test"
for minor in 4 5 6; do
    PYBIN="/opt/python/cp3${minor}-cp3${minor}m/bin"
    "${PYBIN}/pip" install scrappy --no-index -f wheelhouse
    "${PYBIN}/python" -c "from scrappy import *; import numpy as np; print(basecall_raw(np.random.normal(10,4,1000)))" 
done
