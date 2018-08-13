default:
	g++ bot.cpp -std=c++11 -lpthread -O3 -o bot.exe

test:
	g++ tick_test.cpp -std=c++11 -lpthread -lboost_filesystem-mt -lboost_system-mt -o tick_test

profile:
	g++ bot.cpp -std=c++11 -lpthread -O3 -pg -o bot.exe
