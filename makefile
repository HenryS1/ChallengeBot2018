default:
	clang++ bot.cpp -std=c++11 -O3 -o bot.exe

test:
	clang++ tick_test.cpp -std=c++11 -lboost_filesystem-mt -lboost_system-mt -o tick_test
