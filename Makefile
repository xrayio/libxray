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

DEBUG_SANITIZE:=-fsanitize=address -fno-omit-frame-pointer # -fsanitize=undefined
SYS_INC:=-I/usr/include -I/usr/local/include
SYS_LIB:=-L//usr/lib/x86_64-linux-gnu/ -L/usr/local/lib


.PHONY: all pull debug clean libxray libxray-shared test-app xraycli test test-in-docker

all: libxray
	${Q}echo "\033[32mXRAY: done!\033[0m"

init-ubuntu14.04:
	sudo add-apt-repository -y ppa:chris-lea/libsodium
	sudo echo "deb http://ppa.launchpad.net/chris-lea/libsodium/ubuntu trusty main" >> /etc/apt/sources.list
	sudo echo "deb-src http://ppa.launchpad.net/chris-lea/libsodium/ubuntu trusty main" >> /etc/apt/sources.list
	sudo apt-get update -y
	sudo apt-get -y --force-yes install libsodium-dev
	sudo apt-get install -y --force-yes libzmq3 libzmq3-dev
	sudo apt-get install -y --force-yes libpgm-dev

clean:
	${Q}rm -rf ${INSTALL_DIR}/*

run-test-app: test-app
	LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${INSTALL_DIR} ./dist/test-app

libxray:
	g++ -c -g -O2 -I${PKG_INSTALL_DIR} ${SYS_INC} -std=c++11 -fPIC -pthread -o ${INSTALL_DIR}/xray.o ${SRC_DIR}/xray.cpp
	${Q}ar rcs ${INSTALL_DIR}/libxray.a ${INSTALL_DIR}/xray.o 
	${Q}ln -sf ../${SRC_DIR}/xray.h ${INSTALL_DIR}/xray.h

libxray-shared:
	g++ -shared -g -O0 -I${PKG_INSTALL_DIR} ${SRC_DIR}/xray.cpp ${SYS_INC} -lzmq -std=c++11 -fPIC -pthread -o ${INSTALL_DIR}/libxray.so

libxray-debug:
	${Q}g++ -c -g -O0 -I${PKG_INSTALL_DIR} ${SYS_INC} ${DEBUG_SANITIZE} -std=c++11 -fPIC -pthread -o ${INSTALL_DIR}/xray.o ${SRC_DIR}/xray.cpp	
	${Q}ar rcs ${INSTALL_DIR}/libxray.a ${INSTALL_DIR}/xray.o
	${Q}ln -sf ../${SRC_DIR}/xray.h ${INSTALL_DIR}/xray.h

xraycli:
	${Q} pip install -e .

test-app: libxray-shared
	gcc -g -O0 -L${INSTALL_DIR} -I${INSTALL_DIR} ${SYS_INC} ${SYS_LIB} ${DEBUG_SANITIZE} -o ${INSTALL_DIR}/test-app ${SRC_DIR}/test-app.c -lxray -lstdc++ -lzmq

test: test-app xraycli
	${Q}export XRAY_ROOT=$(abspath ${WORK_DIR}); \
	python test/test_unit.py
	${Q}export XRAY_ROOT=$(abspath ${WORK_DIR}); export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${INSTALL_DIR};\
	python test/test_system.py

test-in-docker:
	docker-compose -f docker-test.yml up
