FROM ubuntu:16.04
MAINTAINER grisha85

RUN apt-get update -y
RUN apt-get install -y python build-essential python-pip libnanomsg-dev
ADD . /libxray
RUN pip install /libxray
WORKDIR /libxray
