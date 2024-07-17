# syntax=docker/dockerfile:1
ARG BASE_IMAGE_TAG=latest
FROM ghcr.io/everest/everest-ci/build-env-base:${BASE_IMAGE_TAG}

RUN apt update && apt install --no-install-recommends -y \
    python3-sphinx=5.3.0-4
RUN python3 -m pip install --break-system-packages \
    # Sphinx extensions
    sphinxcontrib-contentui==0.2.5 \
    sphinxcontrib-svg2pdfconverter==1.2.2
