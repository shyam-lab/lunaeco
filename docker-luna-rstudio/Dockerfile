FROM shyamlab/fftw_rstudio:latest

WORKDIR /build

ENV DEBIAN_FRONTEND=noninteractive

COPY luna-base /build/luna-base
COPY luna /build/luna

RUN cd /build \
 && cd luna-base \
 && make FFTW=/build/fftw3 -j 2 \
 && ln -s /build/luna-base/luna /usr/local/bin/luna \
 && ln -s /build/luna-base/destrat /usr/local/bin/destrat \
 && ln -s /build/luna-base/behead /usr/local/bin/behead \
 && cd /build \
 && R CMD build luna \
 && LUNA_BASE=/build/luna-base FFTW=/build/fftw3 R CMD INSTALL luna_0.24.tar.gz
