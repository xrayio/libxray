ifeq ("${V}","1")
	Q :=
else
	Q := @
endif

WORK_DIR:=.
SRC_DIR:=${WORK_DIR}/src
TEST_DIR:=${SRC_DIR}/tests
INSTALL_DIR:=${WORK_DIR}/dist
PKG_DIR:=${WORK_DIR}/pkgs
PKG_INSTALL_DIR:=${PKG_DIR}/install

ZMQ_DIR:=${PKG_DIR}/libzmq
TSL_DIR:=${PKG_DIR}/tsl
ZMQPP_DIR:=${PKG_DIR}/cppzmq
JSON_DIR:=${PKG_DIR}/json


.PHONY: all pull debug clean libxray test-api

all: libxray
	${Q}echo "\033[32mXRAY: done!\033[0m"

init-ubuntu14.04:
	sudo add-apt-repository ppa:chris-lea/libsodium
	sudo echo "deb http://ppa.launchpad.net/chris-lea/libsodium/ubuntu trusty main" >> /etc/apt/sources.list
	sudo echo "deb-src http://ppa.launchpad.net/chris-lea/libsodium/ubuntu trusty main" >> /etc/apt/sources.list
	sudo apt-get update -y
	sudo apt-get -y --force-yes install libsodium-dev
	sudo apt-get install -y --force-yes libzmq3=4.0.4+dfsg-2 libzmq3-dev=4.0.4+dfsg-2
	sudo apt-get install -y --force-yes libpgm-dev
	

pull:
	# git pull --recurse-submodules
	git submodule update --init --recursive

libzmq-fromsrc:
	${Q}echo "building zmq"
	${Q}cd ${ZMQ_DIR} && ./autogen.sh
	${Q}cd ${ZMQ_DIR} && ./configure
	${Q}make -C ${ZMQ_DIR}
	${Q}cp ${ZMQ_DIR}/include/*.h ${PKG_INSTALL_DIR}/
	${Q}cp ${ZMQ_DIR}/src/.libs/*.a ${PKG_INSTALL_DIR}/
	
clean:
	${Q}rm -rf ${INSTALL_DIR}/*

DEBUG_SANITIZE:=-fsanitize=address -fno-omit-frame-pointer
SYS_INC:=-I/usr/include -I/usr/local/include
SYS_LIB:=-L//usr/lib/x86_64-linux-gnu/ -L/usr/local/lib

test-api: libxray-debug
	gcc -g -O0 -L${INSTALL_DIR} -I${INSTALL_DIR} ${SYS_INC} ${SYS_LIB} ${DEBUG_SANITIZE} -lstdc++ -lxray -lzmq -o ${INSTALL_DIR}/test-c-api ${SRC_DIR}/test-c-api.c
	./dist/test-c-api

libxray:
	${Q}g++ -c -g -O2 -I${PKG_INSTALL_DIR} ${SYS_INC} -std=c++11 -fPIC -pthread -o ${INSTALL_DIR}/xray.o ${SRC_DIR}/xray.cpp
	${Q}ar rcs ${INSTALL_DIR}/libxray.a ${INSTALL_DIR}/xray.o 
	${Q}#g++ -shared -o ${INSTALL_DIR}/libxray.so -lzmq -L${INSTALL_DIR} -L/usr/lib/x86_64-linux-gnu/ ${INSTALL_DIR}/xray.o
	${Q}cp ${SRC_DIR}/xray.h ${INSTALL_DIR}/
	${Q}#gcc -g -O0 -L${INSTALL_DIR} -fsanitize=address -lxray -o ${INSTALL_DIR}/xray-ctest ${SRC_DIR}/xray-c.c

libxray-debug:
	${Q}g++ -c -g -O0 -I${PKG_INSTALL_DIR} ${SYS_INC} ${DEBUG_SANITIZE} -std=c++11 -fPIC -pthread -o ${INSTALL_DIR}/xray.o ${SRC_DIR}/xray.cpp	
	${Q}ar rcs ${INSTALL_DIR}/libxray.a ${INSTALL_DIR}/xray.o
	${Q}cp ${SRC_DIR}/xray.h ${INSTALL_DIR}/


