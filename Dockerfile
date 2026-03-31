# syntax=docker/dockerfile:1
#===============================================================================
# NOTE: This docker file is a quick hack to provide a way to test the vmod
#       against multiple varnish versions, in preparation of moving to CI
#       (drone).
#
#-------------------------------------------------------------------------------
# vmod-queryfilter-dev-base: tooling required to build varnish + vmod
#-------------------------------------------------------------------------------

#-------------------------------------------------------------------------------
# pcre3-debs: fetch libpcre3/libpcre3-dev from bookworm (dropped in trixie)
#-------------------------------------------------------------------------------
FROM debian:bookworm-slim AS pcre3-debs

RUN apt-get update && \
	apt-get download libpcre3 libpcre3-dev libpcre16-3 libpcre32-3 libpcrecpp0v5

#-------------------------------------------------------------------------------
# vmod-queryfilter-dev-base: tooling required to build varnish + vmod
#-------------------------------------------------------------------------------
FROM gcc:14 AS vmod-queryfilter-dev-base

COPY --from=pcre3-debs /*.deb /tmp/pcre3/
RUN apt-get update && \
	apt-get install -y \
		git \
		python3-docutils \
		python3-sphinx \
		pkg-config && \
	dpkg -i /tmp/pcre3/*.deb || apt-get install -f -y && \
	rm -rf /tmp/pcre3

#-------------------------------------------------------------------------------
# vmod-queryfilter-varnish: fresh varnish install from source
#-------------------------------------------------------------------------------
ARG VARNISH_VERSION=8.0.1
FROM vmod-queryfilter-dev-base AS vmod-queryfilter-varnish
ENV VARNISH_VERSION=${VARNISH_VERSION}
ENV VARNISHSRC=/src/varnish-${VARNISH_VERSION}

# Clone source with submodules (archive tarballs omit them, breaking configure):
RUN git clone --recursive \
		--branch varnish-${VARNISH_VERSION} \
		https://code.vinyl-cache.org/vinyl-cache/vinyl-cache.git \
		${VARNISHSRC} && \
	cd ${VARNISHSRC} && \
	./autogen.sh && \
	./configure \
		--prefix=/usr/local && \
	make CFLAGS="-Wno-error=format-overflow" && \
	make install

#-------------------------------------------------------------------------------
# vmod-queryfilter-vmod: build and test the vmod
#-------------------------------------------------------------------------------
FROM vmod-queryfilter-varnish AS vmod-queryfilter-test

COPY . /src/libvmod-queryfilter

RUN cd /src/libvmod-queryfilter && \
	./autogen.sh

RUN mkdir -p /src/build && \
	cd /src/build && \
	../libvmod-queryfilter/configure \
		--prefix=/usr/local && \
	make && \
	make check
