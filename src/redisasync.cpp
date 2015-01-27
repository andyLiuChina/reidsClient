#include "redisasync.h"

#include "redisstring.cpp"

#define REDISCONFFILE "/home/lianwo/test/hiredis/dep/redisserver.conf"

map<void*, redisAsyncClient*> redisAsyncClient::clients;

void* dispatch(void* arg)
{
	pthread_detach(pthread_self());
	struct event_base* base = (struct event_base*)arg;
	event_base_dispatch(base);
	return NULL;
}

void commandCallback(redisAsyncContext *c, void *r, void *priv) {
	redisReply *reply = static_cast<redisReply*>(r);
	if (reply == NULL) return;

	if ( strcmp( reply->element[0]->str, "subscribe" ) != 0 ) {
		extract_t* ext = (extract_t*)priv;
		if(priv && ext->f) 
			(ext->f)(reply->element[1]->str, reply->element[2]->str, ext->data);
	}
}

void connectCallback(const redisAsyncContext *c, int status); 

void disconnectCallback(const redisAsyncContext *c, int status) {
	int tmp = REDIS_OK;
	int idx;

	map<void*, redisAsyncClient*>::iterator it = redisAsyncClient::clients.find((void*)c);
	redisAsyncClient* pCli = it->second;

	if (status != REDIS_OK && it != redisAsyncClient::clients.end()) {
	    FPRINTF(stderr, "Error: %s\n", c->errstr);

		if(pCli->c1) {
			redisFree(pCli->c1);
			pCli->c1 = NULL;
		}

		//redisAsyncClient::clients.erase(it);

		//attach to another server ip/port
		idx = ++(pCli->turn);
		strcpy(pCli->_hostip, (pCli->ips)[idx%2]); 
		pCli->_hostport = (pCli->ports)[idx%2];
    		PRINTF("disconnect call back, Remote redis server Has shut down."
			   " Now try to connect another redis server: [ip]: %s [port]: %d\n", pCli->_hostip, pCli->_hostport);

		pCli->c1 = redisConnect(pCli->_hostip, pCli->_hostport);
		if (pCli->c1 != NULL && pCli->c1->err) {
			FPRINTF(stderr,"Could not connect to Redis at ");
			FPRINTF(stderr,"%s:%d: %s\n", pCli->_hostip, pCli->_hostport, pCli->c1->errstr);
			redisFree(pCli->c1);
			pCli->c1 = NULL;
		}
		pCli->c2 = redisAsyncConnect(pCli->_hostip, pCli->_hostport);
		if (pCli->c2->err) {
			FPRINTF(stderr, "Error: %s\n", pCli->c2->errstr);
			redisAsyncFree(pCli->c2);
			pCli->c2 = NULL;
		}
		if(pCli->c2)
			redisAsyncClient::clients.insert(pair<void*, redisAsyncClient*>(pCli->c2, pCli));

    		CHK(redisLibeventAttach(pCli->c2, pCli->base));
		CHK(redisAsyncSetConnectCallback(pCli->c2, connectCallback));
		CHK(redisAsyncSetDisconnectCallback(pCli->c2, disconnectCallback));
		if(!((pCli->subname).empty()))
			pCli->redisSendSub(pCli->subname.c_str());

		close(pCli->check_alive_fd);
		event_del(&pCli->ev);
		CHK(pCli->regist_keepalive(15));

		sleep(1);

		return;
	}

	PRINTF("Disconnected...\n");
}

void connectCallback(const redisAsyncContext *c, int status) {
	int tmp = REDIS_OK;
	int idx;

    if (status != REDIS_OK) {
		FPRINTF(stderr, "Could not connect to Redis Server: %s\n", c->errstr);
		map<void*, redisAsyncClient*>::iterator it = redisAsyncClient::clients.find((void*)c);
		if(it != redisAsyncClient::clients.end()) {
			redisAsyncClient* pCli = it->second;
#if 0
			if(pCli->c1) {
				redisFree(pCli->c1);
				pCli->c1 = NULL;
			}
			//auto destroied after callback	
			if(pCli->c2) {
				redisAsyncFree(pCli->c2);
				pCli->c2 = NULL;
			}
#endif
			//redisAsyncClient::clients.erase(it);

			//attach to another server ip/port
			idx = ++(pCli->turn);
			strcpy(pCli->_hostip, (pCli->ips)[idx%2]); 
			pCli->_hostport = (pCli->ports)[idx%2];
    		PRINTF("connect call back, Try to connect another redis server: [ip]: %s [port]: %d\n", pCli->_hostip, pCli->_hostport);

			pCli->c2 = redisAsyncConnect(pCli->_hostip, pCli->_hostport);
			if (pCli->c2->err) {
				FPRINTF(stderr, "Error: %s\n", pCli->c2->errstr);
				redisAsyncFree(pCli->c2);
				pCli->c2 = NULL;
			}
			if(pCli->c2)
				redisAsyncClient::clients.insert(pair<void*, redisAsyncClient*>(pCli->c2, pCli));

    			CHK(redisLibeventAttach(pCli->c2, pCli->base));
			CHK(redisAsyncSetConnectCallback(pCli->c2, connectCallback));
			CHK(redisAsyncSetDisconnectCallback(pCli->c2, disconnectCallback));
			if(!((pCli->subname).empty()))
				pCli->redisSendSub(pCli->subname.c_str());

			close(pCli->check_alive_fd);
			//event_del(&pCli->ev);
			CHK(pCli->regist_keepalive(15));

			sleep(1);
		}
		return;
	}

    PRINTF("Connected...\n");
}

int redisAsyncClient::redisInit()
{
	int tmp = REDIS_OK;

    signal(SIGPIPE, SIG_IGN);

	//tmp = extractRedisAddress("/home/yangbin/hiredis/dep/redisserver.conf", ips, ports);
	tmp = extractRedisAddress(REDISCONFFILE, ips, ports);
	if(tmp)
		goto end;
	strcpy(_hostip, ips[0]);
	_hostport = ports[0];

	base = event_base_new();
	if(!base) {
		perror("Create event base instance error");
		tmp = REDIS_ERR;
		goto end;
	}

	c1 = redisConnect(_hostip, _hostport);
	if (c1 != NULL && c1->err) {
                //timeout: 20 seconds
                //means network bad when connect first redis server,
                //so try to connect the second redis server.
                if(strstr(c1->errstr, "timed out")) { 
		    redisFree(c1);
                    turn++;
                    strcpy(_hostip, ips[turn%2]);
                     _hostport = ports[turn%2];
                    c1 = redisConnect(_hostip, _hostport);
                    if(c1 && c1->err) { //Shit, it`s impossible for all bad redis server.
                        tmp = c1->err;
                        redisFree(c1);
                        c1 = NULL;
                    }
		} else {
		    tmp = c1->err;
		    FPRINTF(stderr,"Could not connect to Redis at ");
		    FPRINTF(stderr,"%s:%d: %s\n", _hostip, _hostport, c1->errstr);
		    redisFree(c1);
		    c1 = NULL;
               }
	}

	c2 = redisAsyncConnect(_hostip, _hostport);
	if (c2 != NULL && c2->err) {
		tmp = c2->err;
		FPRINTF(stderr, "Error: %s\n", c2->errstr);
		redisAsyncFree(c2);
		c2 = NULL;
	}
	if(c2)
		redisAsyncClient::clients.insert(pair<void*, redisAsyncClient*>(c2, this));

	if(c1)  // ip[0]/port[0] is OK
		CHK(regist_keepalive(15));
//	else { //try to connect ip[1]/port[1]
//		turn++;
//		strcpy(_hostip, ips[turn%2]); 
//		_hostport = ports[turn%2];
//	}

    CHK(redisLibeventAttach(c2, base));
//	CHK(tcpKeepAlive(c2->c.fd, 15));
    CHK(redisAsyncSetConnectCallback(c2, connectCallback));
	CHK(redisAsyncSetDisconnectCallback(c2, disconnectCallback));

	if(!isdispatch) {
		pthread_create(&thread, NULL, dispatch, base);
		isdispatch = true;
		usleep(100);
	}

end:
	return tmp;
}

redisAsyncClient::redisAsyncClient():base(NULL),c1(NULL),c2(NULL)
{
	isdispatch = false;
//	_hostip = const_cast<char*>("127.0.0.1");
	bzero(_hostip, 16);
	strcpy(_hostip, "127.0.0.1");
	_hostport = 6379;
	subname.clear();
	turn = 0;
}

redisAsyncClient::redisAsyncClient(char* hostip, int hostport)
                  :base(NULL),c1(NULL),c2(NULL)
{
	isdispatch = false;
//	_hostip = hostip;
	bzero(_hostip, 16);
	strcpy(_hostip, hostip);
	_hostport = hostport;
	subname.clear();
	turn = 0;
}

redisAsyncClient::~redisAsyncClient()
{
	//it is necessary to do?
	map<void*, redisAsyncClient*>::iterator it = redisAsyncClient::clients.begin();
	for(; it != redisAsyncClient::clients.end(); it++)
		redisAsyncClient::clients.erase(it);
				
	close(check_alive_fd);

	if(c2) {
		redisAsyncFree(c2);
		c2 = NULL;
	}
	if(c1) {
		redisFree(c1);
		c1 = NULL;
	}
	if(base) {
		event_base_loopbreak(base);
		event_base_free(base);
		base = NULL;
		pthread_join(thread, NULL);
	}
}

void redisAsyncClient::redisSetSubCallback(extractValueFn fun, void* userdata)
{
	extract.f = fun;
	extract.data = userdata;
}

int redisAsyncClient::redisSendSub(const char* channel)
{
	subname.assign(channel);
	string command = "SUBSCRIBE ";
	command += channel;

	return redisAsyncCommand(c2, commandCallback, (void*)&extract, command.c_str());
}

#if 0
int redisAsyncClient::redisSendPub(char* channel, char* value)
{
	string command = "PUBLISH ";
	command += channel;
	command += " ";
	command += value;

	return redisAsyncCommand(c2, NULL, NULL, command.c_str());
}
#endif

int redisAsyncClient::redisSendPub(char* channel, char* value)
{
	int tmp = REDIS_OK;

	string command = "PUBLISH ";
	command += channel;
	command += " ";
	command += value;

	if(c1 == NULL) {
		c1 = redisConnect(_hostip, _hostport);
		if (c1->err) {
			tmp = c1->err;
			FPRINTF(stderr,"Could not connect to Redis at ");
			FPRINTF(stderr,"%s:%d: %s\n", _hostip, _hostport, c1->errstr);
			redisFree(c1);
			c1 = NULL;
			return tmp;
		}
		PRINTF("connected\n");
	}

	redisReply *reply = NULL;

	reply = (redisReply*)redisCommand(c1, command.c_str());
	if(!reply) {
		tmp = c1->err;
		redisFree(c1);
		c1 = NULL;
		return tmp;
	}

	freeReplyObject(reply);
	return tmp;
}

int redisAsyncClient::redisSendSet(char* channel, char* value)
{
	int tmp = REDIS_OK;

	string command = "SET ";
	command += channel;
	command += " ";
	command += value;

	if(c1 == NULL) {
		c1 = redisConnect(_hostip, _hostport);
		if (c1->err) {
			tmp = c1->err;
			FPRINTF(stderr,"Could not connect to Redis at ");
			FPRINTF(stderr,"%s:%d: %s\n", _hostip, _hostport, c1->errstr);
			redisFree(c1);
			c1 = NULL;
			return tmp;
		}
		PRINTF("connected\n");
	}

	redisReply *reply = NULL;

	reply = (redisReply*)redisCommand(c1, command.c_str());
	if(!reply) {
		tmp = c1->err;
		redisFree(c1);
		c1 = NULL;
		return tmp;
	}

	freeReplyObject(reply);
	return tmp;
}

int redisAsyncClient::redisSendSet(char* channel, char* value, int seconds)
{
	int tmp = REDIS_OK;

	string command = "SET ";
	command += channel;
	command += " ";
	command += value;

	if(c1 == NULL) {
		c1 = redisConnect(_hostip, _hostport);
		if (c1 != NULL && c1->err) {
			tmp = c1->err;
			FPRINTF(stderr,"Could not connect to Redis at ");
			FPRINTF(stderr,"%s:%d: %s\n", _hostip, _hostport, c1->errstr);
			redisFree(c1);
			c1 = NULL;
			return tmp;
		}
		PRINTF("connected\n");
	}

	redisReply *reply = NULL;

	reply = (redisReply*)redisCommand(c1, command.c_str());
	if(!reply) {
		tmp = c1->err;
		redisFree(c1);
		c1 = NULL;
		return tmp;
	}

	freeReplyObject(reply);

	char buf[32] = {0};
	command.clear();
	command = "EXPIRE ";
	command += channel;
	command += " ";
	sprintf(buf, "%d", seconds);
	command += buf;
	reply = NULL;
	reply = (redisReply*)redisCommand(c1, command.c_str());
	if(!reply) {
		tmp = c1->err;
		redisFree(c1);
		c1 = NULL;
		return tmp;
	}

	freeReplyObject(reply);
	return tmp;
}

int redisAsyncClient::redisSendGet(char* line, string& retval)
{
	int tmp = REDIS_OK;

	retval.clear();

	string command = "GET ";
	redisReply *reply = NULL;
	command += line;

	if(c1 == NULL) {
		c1 = redisConnect(_hostip, _hostport);
		if (c1 != NULL && c1->err) {
			tmp = c1->err;
			FPRINTF(stderr,"Could not connect to Redis at ");
			FPRINTF(stderr,"%s:%d: %s\n", _hostip, _hostport, c1->errstr);
			redisFree(c1);
			c1 = NULL;
			goto end;
		}
		PRINTF("connected\n");
	}

	reply = (redisReply*)redisCommand(c1, command.c_str());
	if(reply == NULL) {
		tmp = c1->err;
		if(c1) {
			redisFree(c1);
			c1 = NULL;
		}
	} else {
		switch(reply->type) {
		case REDIS_REPLY_STRING:
		case REDIS_REPLY_STATUS:
	   		retval	+= reply->str;
		break;
		case REDIS_REPLY_INTEGER:
			retval += reply->integer;
		break;
		case REDIS_REPLY_NIL:
		break;
		case REDIS_REPLY_ERROR:
			tmp = c1->err;
		break;
		}

		freeReplyObject(reply);
	}

end:
	return tmp;
}

int redisAsyncClient::tcpKeepAlive(int fd, int interval)
{
    int val = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1)
    {
        FPRINTF(stderr, "setsockopt SO_KEEPALIVE: %s", strerror(errno));
        return REDIS_ERR;
    }

#ifdef __linux__
    /* Default settings are more or less garbage, with the keepalive time
     * set to 7200 by default on Linux. Modify settings to make the feature
     * actually useful. */

    /* Send first probe after interval. */
    val = interval;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) < 0) {
        FPRINTF(stderr, "setsockopt TCP_KEEPIDLE: %s\n", strerror(errno));
        return REDIS_ERR;
    }

    /* Send next probes after the specified interval. Note that we set the
     * delay as interval / 3, as we send three probes before detecting
     * an error (see the next setsockopt call). */
    val = interval/3;
    if (val == 0) val = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) < 0) {
        FPRINTF(stderr, "setsockopt TCP_KEEPINTVL: %s\n", strerror(errno));
        return REDIS_ERR;
    }

    /* Consider the socket in error state after three we send three ACK
     * probes without getting a reply. */
    val = 3;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) < 0) {
        FPRINTF(stderr, "setsockopt TCP_KEEPCNT: %s\n", strerror(errno));
        return REDIS_ERR;
    }
#else
    ((void) interval); /* Avoid unused var warning for non Linux systems. */
#endif

    return REDIS_OK;
}

void check_callback(int fd, short events, void* arg)
{
	int tmp = REDIS_OK;

	redisAsyncClient* pCli = (redisAsyncClient*)arg;

	int buf[128] = {0};
	int idx;

	PRINTF("call check_callback()\n");
	PRINTF("events [%d]\n", events);
	if(events == EV_READ) {
		if((recv(fd, buf, 128, 0) == -1) && (errno == ETIMEDOUT)) {
			PRINTF("call check_callback() and set\n");
			(pCli->turn)++;
			idx = (pCli->turn) % 2;
			//pCli->_hostip = pCli->ips[idx];
			strcpy(pCli->_hostip,  pCli->ips[idx]);
			pCli->_hostport = pCli->ports[idx];

			//need reconnect to another redis-server, and set callback,attach to event_base etc.
			//redisAsyncClient::clients.erase(pCli->c2);
			close(pCli->check_alive_fd);
			if(pCli->c1) {
				redisFree(pCli->c1);
				pCli->c1 = NULL;
			}
			if(pCli->c2) {
				redisAsyncFree(pCli->c2);
				pCli->c2 = NULL;
			}
		
			PRINTF("network problem, reconnect another redos: [ip]: %s [port]: %d\n", pCli->_hostip, pCli->_hostport);
			pCli->c1 = redisConnect(pCli->_hostip, pCli->_hostport);
			if (pCli->c1 != NULL && pCli->c1->err) {
				FPRINTF(stderr, "%s(%d)-<%s>: ",  __FILE__, __LINE__, __FUNCTION__);
				FPRINTF(stderr,"Could not connect to Redis at ");
				FPRINTF(stderr,"%s:%d: %s\n", pCli->_hostip, pCli->_hostport, pCli->c1->errstr);
				redisFree(pCli->c1);
				pCli->c1 = NULL;
			}
			pCli->c2 = redisAsyncConnect(pCli->_hostip, pCli->_hostport);
			if (pCli->c2->err) {
				FPRINTF(stderr, "Error: %s\n", pCli->c2->errstr);
				redisAsyncFree(pCli->c2);
				pCli->c2 = NULL;
			}
			if(pCli->c2)
				redisAsyncClient::clients.insert(pair<void*, redisAsyncClient*>(pCli->c2, pCli));
		
//			pCli->changerole();
			event_del(&pCli->ev); //remove previous event
			pCli->regist_keepalive(15); //regist new time event
			CHK(redisLibeventAttach(pCli->c2, pCli->base));
			CHK(redisAsyncSetConnectCallback(pCli->c2, connectCallback));
			CHK(redisAsyncSetDisconnectCallback(pCli->c2, disconnectCallback));
			if(!((pCli->subname).empty()))
				pCli->redisSendSub(pCli->subname.c_str());

			sleep(1);
		}
	}
}

int redisAsyncClient::regist_keepalive(int interval) 
{
	int tmp = REDIS_OK;
//	int check_alive_fd;
  
	//detect network anomaly
	check_alive_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(check_alive_fd == -1) {
		perror("Create scoket for checking alive failed");
		return REDIS_ERR;
	}

  	//set no block
	//evutil_make_socket_nonblocking(check_alive_fd);
	int flags;
	if ((flags = fcntl(check_alive_fd, F_GETFL, NULL)) == -1) {
		FPRINTF(stderr, "fcntl(%d, F_GETFL)", check_alive_fd);
		return REDIS_ERR;;
	}
	if (fcntl(check_alive_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
		FPRINTF(stderr, "fcntl(%d, F_SETFL)", check_alive_fd);
		return REDIS_ERR;;
	}
	tmp = tcpKeepAlive(check_alive_fd, interval);

	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);

	memset(&addr, 0, len);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(_hostport);
	addr.sin_addr.s_addr = inet_addr(_hostip);	

	if(connect(check_alive_fd, (struct sockaddr*)&addr, len) == -1) {
		if(errno != EINPROGRESS) {
			perror("Connect redis server failed");
			tmp = REDIS_ERR;
		}
	}

	PRINTF("regist call back and fd is: %d\n", check_alive_fd);
	memset(&ev, 0, sizeof(struct event));
	event_set(&ev, check_alive_fd, EV_READ /*EV_PERSIST*/, check_callback, this);
	event_base_set(base, &ev);
	event_add(&ev, NULL);

	return tmp;
}
