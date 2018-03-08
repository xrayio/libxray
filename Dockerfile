FROM ubuntu:16.04
MAINTAINER grisha85

RUN apt update -y
RUN apt install -y libzmq3-dev libsodium-dev python build-essential python-pip

ADD . /libxray
WORKDIR /libxray
