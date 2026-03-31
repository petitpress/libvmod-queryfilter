#!/bin/bash
set -eo pipefail
repodir="$( cd "${0%/*}"/.. || return ; echo "${PWD}" )"

DEFAULT_VARNISH_VERSIONS="6.0.17 7.6.5 7.7.3 8.0.1"
VARNISH_VERSIONS="${VARNISH_VERSIONS:-$DEFAULT_VARNISH_VERSIONS}"

queryfilter_test() {
    cd "${repodir}" || exit 1
    docker build . \
        --build-arg "VARNISH_VERSION=${VARNISH_VERSION}" \
        -t "libvmod-queryfilter:local-${VARNISH_VERSION}"
}

for VARNISH_VERSION in ${VARNISH_VERSIONS}; do
    export VARNISH_VERSION
    queryfilter_test
done
