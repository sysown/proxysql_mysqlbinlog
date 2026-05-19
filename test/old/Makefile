.PHONY: default
default: test1 test2 test3 test4 test5

test1: test1.cpp
	g++ -o test1 -std=c++11 test1.cpp -ggdb ../libslave/build/libslave.a -I../libslave/ -rdynamic ../libslave/build/libslave.a -lz -ldl -lpthread -lboost_system -lrt -Wl,-Bstatic -lmysqlclient -Wl,-Bdynamic -ldl

test2: test2.cpp
	g++ -o test2 -std=c++11 test2.cpp -ggdb ../libslave/build/libslave.a -I../libslave/ -rdynamic ../libslave/build/libslave.a -lz -ldl -lpthread -lboost_system -lrt -Wl,-Bstatic -lmysqlclient -Wl,-Bdynamic -ldl

test3: test3.cpp
	g++ -o test3 -std=c++11 test3.cpp -ggdb ../libslave/build/libslave.a -I../libslave/ -rdynamic ../libslave/build/libslave.a -lz -ldl -lpthread -lboost_system -lrt -Wl,-Bstatic -lmysqlclient -Wl,-Bdynamic -ldl

test4: test4.cpp
	g++ -o test4 -std=c++11 test4.cpp -ggdb ../libslave/build/libslave.a -I../libslave/ -rdynamic ../libslave/build/libslave.a -lz -ldl -lpthread -lboost_system -lrt -Wl,-Bstatic -lmysqlclient -Wl,-Bdynamic -ldl -lev

test5: test5.cpp
	g++ -o test5 -std=c++11 test5.cpp -ggdb ../libslave/build/libslave.a -I../libslave/ -rdynamic ../libslave/build/libslave.a -lz -ldl -lpthread -lboost_system -lrt -Wl,-Bstatic -lmysqlclient -Wl,-Bdynamic -ldl -lev -ldaemon

