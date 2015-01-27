#ifndef __REDIS_ASYNC__
#define __REDIS_ASYNC__

#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <map>

#include <fcntl.h>
#include <arpa/inet.h>

#include "hiredis.h"
#include "async.h"
#include "libevent.h"

using namespace std;

#define CHK(exp)                              \
		do {                                  \
			if(exp) {                         \
				perror("exp");                \
				tmp = -1;                     \
			}	                              \
		} while(0)

#define CHK1(res, exp)                        \
		do {                                  \
			if(((res)=(exp))) {               \
				perror("exp");                \
				tmp = -1;                     \
			}                                 \
		} while(0)

#ifdef DEBUG
	#define PRINTF printf
	#define FPRINTF fprintf
#else
	#define PRINTF(X, ...)
	#define FPRINTF(a,b,...)
#endif

typedef void (*extractValueFn)(char* channel, char* value, void* userdata);
typedef struct {
	extractValueFn f;
	void* data;
}extract_t;


class redisAsyncClient
{
public:
	int redisInit();

	redisAsyncClient();
	redisAsyncClient(char* hostip, int hostport);
	~redisAsyncClient();

	void redisSetSubCallback(extractValueFn fun, void* userdata);

	int redisSendSub(const char* command);
	int redisSendPub(char* channel, char* value);
	int redisSendGet(char* command, string& retval);
	int redisSendSet(char* channel, char* value);
	int redisSendSet(char* channel, char* value, int seconds);
	int regist_keepalive(int interval);
private:
	bool isdispatch;
	char _hostip[16];
	int _hostport;
	pthread_t thread;

	string subname;

	extract_t extract;
	struct event_base* base;
	redisContext* c1;  //get | set
	redisAsyncContext* c2;  //pub | sub

	int check_alive_fd;
	struct event ev;
	int turn;         //a flag to judge which redis server to be used
	char ips[2][16];  //save ips which readed from configure file
	int ports[2];     //save ports, default 6379
	int tcpKeepAlive(int fd, int interval);

	inline void changerole() { redisAsyncCommand(c2, NULL, NULL, "SLAVEOF no one"); } //master < -- > slave
public:
	friend void* dispatch(void* arg);
	friend void commandCallback (redisAsyncContext* c, void* r, void* priv);
    friend void connectCallback(const redisAsyncContext *c, int status);
    friend void disconnectCallback(const redisAsyncContext *c, int status);
	friend void check_callback(int, short, void*);

public:
	static map<void*, redisAsyncClient*> clients;
};

#endif //end of __REDIS_ASYNC__
