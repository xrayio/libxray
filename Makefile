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

TSL_DIR:=${PKG_DIR}/tsl
JSON_DIR:=${PKG_DIR}/json

DEBUG_SANITIZE:=-fsanitize=address -fno-omit-frame-pointer # -fsanitize=undefined
SYS_INC:=-I/usr/include -I/usr/local/include
SYS_LIB:=-L//usr/lib/x86_64-linux-gnu/ -L/usr/local/lib

RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

.PHONY: all pull debug clean libxray libxray-shared test-app xraycli test test-in-docker

all: libxray
	${Q}echo "\033[32mXRAY: done!\033[0m"

clean:
	${Q}rm -rf ${INSTALL_DIR}/*

run-test-app: test-app
	LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${INSTALL_DIR} ./dist/test-app

libxray:
	g++ -c -g -O2 -I${PKG_INSTALL_DIR} ${SYS_INC} -std=c++11 -fPIC -pthread -o ${INSTALL_DIR}/xray.o ${SRC_DIR}/xray.cpp
	${Q}ar rcs ${INSTALL_DIR}/libxray.a ${INSTALL_DIR}/xray.o 
	${Q}ln -sf ../${SRC_DIR}/xray.h ${INSTALL_DIR}/xray.h

libxray-shared:
	g++ -shared -g -O0 -I${PKG_INSTALL_DIR} ${SRC_DIR}/xray.cpp ${SYS_INC} -lnanomsg -std=c++11 -fPIC -pthread -o ${INSTALL_DIR}/libxray.so

libxray-debug:
	${Q}g++ -c -g -O0 -I${PKG_INSTALL_DIR} ${SYS_INC} ${DEBUG_SANITIZE} -std=c++11 -fPIC -pthread -o ${INSTALL_DIR}/xray.o ${SRC_DIR}/xray.cpp
	${Q}ar rcs ${INSTALL_DIR}/libxray.a ${INSTALL_DIR}/xray.o
	${Q}ln -sf ../${SRC_DIR}/xray.h ${INSTALL_DIR}/xray.h

xraycli:
	${Q} pip install -e .

test-app: libxray-shared
	gcc -g -O0 -L${INSTALL_DIR} -I${INSTALL_DIR} ${SYS_INC} ${SYS_LIB} ${DEBUG_SANITIZE} -o ${INSTALL_DIR}/test-app ${SRC_DIR}/test-app.c -lxray -lstdc++ -lnanomsg

test: test-app xraycli
	echo -e "I ${BLUe}Starting unit tests${NC} "
	${Q}export XRAY_ROOT=$(abspath ${WORK_DIR}); \
	python test/test_unit.py
	${Q}export XRAY_ROOT=$(abspath ${WORK_DIR}); export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${INSTALL_DIR};\
	python test/test_system.py

test-in-docker:
	docker-compose -f ./docker-test.yml build
	docker-compose -f docker-test.yml up
