CXX ?= g++

DEBUG ?= 1

ifeq ($(DEBUG),1)
	CXXFLAGS+=-g
else
	CXXFLAGS+=-O2
endif

DEPS = ./CGImysql/SqlConnectionPool.d ./config/config.d ./HttpConn/HttpConn.d ./log/log.d ./timer/lst_timer.d ./WebServer/WebServer.d

server:main.o ./CGImysql/SqlConnectionPool.o ./config/config.o ./HttpConn/HttpConn.o ./log/log.o ./timer/lst_timer.o ./WebServer/WebServer.o
	@echo "building ..."
	$(CXX) -o $@ $^ $(CXXFLAGS) -pthread -lmysqlclient

%.d:%.cpp
	rm -f $@;\
	$(CC) -MM $< >$@.tmp;\
	sed 's,\($*\)\.o[ :]*,\1.o $@:,g' <$@.tmp >$@;\
	rm -f $@.tmp

%.o:%.cpp
	@echo $@
	$(CXX) -c -o $@ $<

clean:
	@echo "clean ..."
	rm -f server *.o $(DEPS)

-include $(DEPS)




