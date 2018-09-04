GTEST=-I/usr/local/include/gtest/

.PHONY:
	test 

default:
	g++ search.cpp -Wall -std=c++11 -lpthread -O3 -o bot.exe

test: 
	g++ test.cpp -Wall -std=c++11 -lgtest -O3 -o test

tick_test:
	g++ tick_test.cpp -std=c++11 -lpthread -lboost_filesystem-mt -lboost_system-mt -o tick_test

