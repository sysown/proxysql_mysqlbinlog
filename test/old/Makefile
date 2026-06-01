
.PHONY: default
default: test_libslave-1 test_libslave-2 test_libslave-3 test_libslave-4 test_libslave-5

test_libslave-1: test_libslave-1.cpp
	g++ -o test_libslave-1 -std=c++11 test_libslave-1.cpp -ggdb ../libslave/build/libslave.a -I../libslave/ -rdynamic ../libslave/build/libslave.a -lz -ldl -lpthread -lboost_system -lrt -Wl,-Bstatic -lmysqlclient -Wl,-Bdynamic -ldl

test_libslave-2: test_libslave-2.cpp
	g++ -o test_libslave-2 -std=c++11 test_libslave-2.cpp -ggdb ../libslave/build/libslave.a -I../libslave/ -rdynamic ../libslave/build/libslave.a -lz -ldl -lpthread -lboost_system -lrt -Wl,-Bstatic -lmysqlclient -Wl,-Bdynamic -ldl

test_libslave-3: test_libslave-3.cpp
	g++ -o test_libslave-3 -std=c++11 test_libslave-3.cpp -ggdb ../libslave/build/libslave.a -I../libslave/ -rdynamic ../libslave/build/libslave.a -lz -ldl -lpthread -lboost_system -lrt -Wl,-Bstatic -lmysqlclient -Wl,-Bdynamic -ldl

test_libslave-4: test_libslave-4.cpp
	g++ -o test_libslave-4 -std=c++11 test_libslave-4.cpp -ggdb ../libslave/build/libslave.a -I../libslave/ -rdynamic ../libslave/build/libslave.a -lz -ldl -lpthread -lboost_system -lrt -Wl,-Bstatic -lmysqlclient -Wl,-Bdynamic -ldl -lev

test_libslave-5: test_libslave-5.cpp
	g++ -o test_libslave-5 -std=c++11 test_libslave-5.cpp -ggdb ../libslave/build/libslave.a -I../libslave/ -rdynamic ../libslave/build/libslave.a -lz -ldl -lpthread -lboost_system -lrt -Wl,-Bstatic -lmysqlclient -Wl,-Bdynamic -ldl -lev -ldaemon
