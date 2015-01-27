//
//extract redis server ip/port
//
//NOTE: the configure file must according to the strict requirements

#include <fstream>

int extractRedisAddress(const char* file, char (*ip)[16], int* port)
{
	char* pos = NULL, *nul = NULL;
	char buf[256] = {0}; //store each line contant

	int ip_idx = 0, port_idx = 0;

	ifstream pfile(file);
	if(!pfile.is_open()) {
		perror("Open redis server address configure file error");
		return REDIS_ERR;	
	}

	while(!pfile.eof()) {
		bzero(buf, 256);
		pfile.getline(buf, 256);

		if(buf[0] != '#' && buf[0] != 0X00) {
			if(strstr(buf, "server1") || strstr(buf, "server2"))
				continue;

			if((pos = strstr(buf, "ip"))) {
				if((nul = strchr(pos, ' ')))
					*nul = '\0';
				strcpy(ip[ip_idx], pos + 3);
				ip_idx++;
			}
			else if ((pos = strstr(buf, "port"))) {
				if((nul = strchr(pos, ' ')))
					*nul = '\0';
				port[port_idx] = atoi(pos + 5);
				port_idx++;
			}
		}

		if((ip_idx == 2) && (port_idx == 2))
			break;
	}

	pfile.close();
	return REDIS_OK;
}
