FROM ubuntu:16.04
MAINTAINER grisha85

RUN apt update -y
RUN apt install -y libzmq3-dev libsodium-dev python build-essential python-pip
RUN pip install zmq tabulate pandas # xraycli deps
ADD . /libxray
WORKDIR /libxray
