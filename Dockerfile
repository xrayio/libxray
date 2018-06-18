FROM ubuntu:16.04
MAINTAINER grisha85

RUN apt-get update -y
RUN apt-get install -y python build-essential python-pip libnanomsg-dev
RUN pip install ipdb
RUN pip install nanomsg tabulate pandas click # for caching, faster xraycli install
ADD . /libxray
RUN pip install /libxray
WORKDIR /libxray
