#include <sstream>
#include <signal.h>

#include <assert.h>
#include <ev.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <iostream>
#include <vector>
#include <algorithm>


#include "Slave.h"
#include "DefaultExtState.h"

#define ioctl_FIONBIO(fd, mode) \
{ \
  int ioctl_mode=mode; \
  ioctl(fd, FIONBIO, (char *)&ioctl_mode); \
}

unsigned int listen_port = 6020;
struct ev_async async;
std::vector<struct ev_io *> Clients;

pthread_mutex_t pos_mutex;
std::vector<char *> server_uuids;
std::vector<uint64_t> trx_ids;

std::string gtid_executed_to_string(slave::Position &curpos);

#define NETBUFLEN 256

class Client_Data {
	public:
	char *data;
	size_t len;
	size_t size;
	size_t pos;
	struct ev_io *w;
	char uuid_server[64];
	Client_Data(struct ev_io *_w) {
		w = _w;
		size = NETBUFLEN;
		data = (char *)malloc(size);
		uuid_server[0] = 0;
		pos = 0;
		len = 0;
	}
	void resize(size_t _s) {
		char *data_ = (char *)malloc(_s);
		memcpy(data_, data, (_s > size ? size : _s));
		size = _s;
		free(data);
		data = data_;
	}
	void add_string(const char *_ptr, size_t _s) {
		if (size < len + _s) {
			resize(len + _s);
		}
		memcpy(data+len,_ptr,_s);
		len += _s;
	}
	~Client_Data() {
		free(data);
	}
	bool writeout() {
		bool ret = true;
		if (len==0) {
			return ret;
		}
		int rc = 0;
		rc = write(w->fd,data+pos,len-pos);
		if (rc > 0) {
			pos += rc;
			if (pos >= len/2) {
				memmove(data,data+pos,len-pos);
				len = len-pos;
				pos = 0;
			}
		}
		return ret;
	}
};


void read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) {
	std::vector<struct ev_io *>::iterator it;
	it = std::find(Clients.begin(), Clients.end(), watcher);
	if (it != Clients.end()) {
		std::cout << "Remove client with FD " << watcher->fd << std::endl;
		Clients.erase(it);
	}
	if(EV_ERROR & revents) {
		perror("got invalid event");
		ev_io_stop(loop,watcher);
		close(watcher->fd);
		Client_Data *custom_data = (Client_Data *)watcher->data;
		delete custom_data;
		watcher->data = NULL;
		free(watcher);
		return;
	}
	close(watcher->fd);
	ev_io_stop(loop,watcher);
	free(watcher);
}

#define NGTIDS 2000000
volatile sig_atomic_t stopflag = 0;
slave::Slave* sl = NULL;

slave::Position curpos;

int pipefd[2];

void accept_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) {
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	int client_sd;
	struct ev_io *client = (struct ev_io*) malloc(sizeof(struct ev_io));
	if(EV_ERROR & revents) {
		perror("got invalid event");
		free(client);
		return;
	}

	// Accept client request
	client_sd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_len);
	if (client_sd < 0) {
		perror("accept error");
		free(client);
		return;
	}
	Client_Data * custom_data = new Client_Data(client);
	client->data = (void *)custom_data;
	ev_io_init(client, read_cb, client_sd, EV_READ);
	ev_io_start(loop, client);
	pthread_mutex_lock(&pos_mutex);
	std::string s1 = gtid_executed_to_string(curpos);
	pthread_mutex_unlock(&pos_mutex);
	s1 = "ST=" + s1 + "\n";
	custom_data->add_string(s1.c_str(), s1.length());
	std::cout << "Push back client with FD " << client->fd << std::endl;
	custom_data->writeout();
	Clients.push_back(client);
}

/*void timeout_cb(struct ev_loop *loop, struct ev_timer *watcher, int revents) {
	std::cout << "Timeout..." << std::endl;
	ev_timer_again (loop, watcher);
}
*/

void async_cb(struct ev_loop *loop, struct ev_async *watcher, int revents) {
	pthread_mutex_lock(&pos_mutex);
	//std::cout << sl->gtid_next.first << ":" << sl->gtid_next.second << std::endl;
	//std::string s = sl->gtid_next.first;
	//s.append(":");
	//s += std::to_string(sl->gtid_next.second);
	//s += '\n';
	for (std::vector<struct ev_io *>::iterator it=Clients.begin(); it!=Clients.end(); ++it) {
		struct ev_io *w = *it;
		Client_Data * custom_data = (Client_Data *)w->data;
		for (std::vector<char *>::size_type i=0; i<server_uuids.size(); i++) {
			//if (custom_data->uuid_server[0]==0 || memcmp(custom_data->uuid_server,sl->gtid_next.first.c_str(),sl->gtid_next.first.length())) {
			if (custom_data->uuid_server[0]==0 || strncmp(custom_data->uuid_server, server_uuids.at(i), strlen(server_uuids.at(i)))) {
				//memcpy(custom_data->uuid_server,sl->gtid_next.first.c_str(),sl->gtid_next.first.length());
				strcpy(custom_data->uuid_server,server_uuids.at(i));
				custom_data->add_string((const char *)"I1=",3);
				std::string s2 = server_uuids.at(i);
				s2 += ":" + std::to_string(trx_ids.at(i)) + "\n";
				//s2 += ":" + std::to_string(sl->gtid_next.second);	
				custom_data->add_string(s2.c_str(),s2.length());
			} else {
				//custom_data->add_string((const char *)"I2=",3);
				//std::string s2 = std::to_string(sl->gtid_next.second);
				//s2 += '\n';
				//custom_data->add_string(s2.c_str(),s2.length());
				std::string s2 = "I2=" + std::to_string(trx_ids.at(i)) + "\n";
				custom_data->add_string(s2.c_str(),s2.length());
			}
		}
		custom_data->writeout();
		//write(w->fd,s.c_str(),s.length());
	}
	for (std::vector<char *>::size_type i=0; i<server_uuids.size(); i++) {
		free(server_uuids.at(i));
	}
	server_uuids.clear();
	trx_ids.clear();
	
	pthread_mutex_unlock(&pos_mutex);
	return;
}


static void sigint_cb (struct ev_loop *loop, ev_signal *w, int revents) {
	stopflag = 1;
	sl->close_connection();
	std::cout << " Received signal. Stopping at:" << std::endl;
	std::string s1 = gtid_executed_to_string(curpos);
	std::cout << s1 << std::endl;
	ev_break (loop, EVBREAK_ALL);
}

static struct ev_loop *loop;
class GTID_Server_Dumper {
	private:
	struct sockaddr_in addr;
	int sd;
	int port;
	struct ev_io ev_accept;
	struct ev_loop *my_loop;
	struct ev_timer timer;
	public:
	GTID_Server_Dumper(int _port) {
		port = _port;
		sd = socket(PF_INET, SOCK_STREAM, 0);
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		addr.sin_addr.s_addr = INADDR_ANY;
		int arg_on = 1;
		if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, (char *)&arg_on, sizeof(arg_on)) == -1) {
			perror("setsocketopt()");
			close(sd);
			exit(EXIT_FAILURE);
		}

		if (bind(sd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
			perror("bind");
			exit(EXIT_FAILURE);
		}
		ioctl_FIONBIO(sd,1);
		listen(sd,10);
		//struct ev_loop *my_loop = NULL;
		my_loop = NULL;
		my_loop = ev_loop_new (EVBACKEND_POLL | EVFLAG_NOENV);
		loop = my_loop;
		if (my_loop == NULL) {
			fprintf(stderr,"could not initialise new loop");
			exit(EXIT_FAILURE);
		}
		ev_io_init(&ev_accept, accept_cb, sd, EV_READ);
		ev_io_start(my_loop, &ev_accept);
		ev_async_init(&async, async_cb);
		ev_async_start(my_loop, &async);
		ev_signal signal_watcher1;
		ev_signal signal_watcher2;
		ev_signal_init (&signal_watcher1, sigint_cb, SIGINT);
		ev_signal_init (&signal_watcher2, sigint_cb, SIGTERM);
		ev_signal_start (loop, &signal_watcher1);
		ev_signal_start (loop, &signal_watcher2);
		//ev_timer_init (&timer, timeout_cb, 5.5, 0.);
		//timer.repeat = 1.;
		//ev_timer_again (my_loop, &timer);
		//ev_timer_start (my_loop, &timer);
		ev_run(my_loop, 0);
	}
	~GTID_Server_Dumper() {
		close(sd);
	}
};



void bench_xid_callback(unsigned int server_id) {
	pthread_mutex_lock(&pos_mutex);
	std::cout << sl->gtid_next.first << ":" << sl->gtid_next.second << std::endl;
	char *str=strdup(sl->gtid_next.first.c_str());
	server_uuids.push_back(str);	
	trx_ids.push_back(sl->gtid_next.second);	
	curpos.addGtid(sl->gtid_next);
	pthread_mutex_unlock(&pos_mutex);
	ev_async_send(loop, &async);
}

/*
void sighandler(int sig) {
	stopflag = 1;
	sl->close_connection();
	std::cout << " Received signal. Stopping at:" << std::endl;
	std::string s1 = gtid_executed_to_string(curpos);
	std::cout << s1 << std::endl;
	ev_break(EV_A_ EVBREAK_ALL);
}
*/



bool isStopping() {
	return stopflag;
}

std::string gtid_executed_to_string(slave::Position &curpos) {
	std::string gtid_set;
	for (auto it=curpos.gtid_executed.begin(); it!=curpos.gtid_executed.end(); ++it) {
		std::string s = it->first;
		s.insert(8,"-");
		s.insert(13,"-");
		s.insert(18,"-");
		s.insert(23,"-");
		s = s + ":";
	   	//std::cout << s << '\n';
		for (auto itr = it->second.begin(); itr != it->second.end(); ++itr) {
			std::string s2 = s;
			s2 = s2 + std::to_string(itr->first);
			s2 = s2 + "-";
			s2 = s2 + std::to_string(itr->second);
	   		//std::cout << s2 << '\n';
			s2 = s2 + ",";
			gtid_set = gtid_set + s2;
		}
	}
	gtid_set.pop_back();
	return gtid_set;
}


void usage(const char* name) {
	std::cout << "Usage: " << name << " -h <mysql host> -u <mysql user> -p <mysql password> -P <mysql port>" << std::endl;
}


void * server(void *args) {
	GTID_Server_Dumper * serv_dump = new GTID_Server_Dumper(listen_port);
	return NULL;
}

/*
void *pipe_reader (void *args) {
	char buff[128];
	char uuid[33];
	uint64_t id;
	uuid[32]=0;
	for (int i=0; i<NGTIDS; i++) {
		read(pipefd[0],buff,40);
		memcpy(uuid,buff,32);
		memcpy(&id,buff+32,sizeof(uint64_t));
		//std::cout << uuid << ":" << id << std::endl;
		std::string suuid = uuid;
		std::pair<std::string, int64_t> gtid_next = std::make_pair(suuid,id);
		curpos.addGtid(gtid_next);
	}
	return NULL;
}
*/

int main(int argc, char** argv) {
	std::string host;
	std::string user;
	std::string password;
	unsigned int port = 3306;


	pthread_mutex_init(&pos_mutex, NULL);


	int c;
	while (-1 != (c = ::getopt(argc, argv, "h:u:p:P:l:"))) {   
		switch (c) {
			case 'h': host = optarg; break;
			case 'u': user = optarg; break;
			case 'p': password = optarg; break;
			case 'P': port = std::stoi(optarg); break;
			case 'l': listen_port = std::stoi(optarg); break;
			default:
				usage(argv[0]);
				return 1;
		}
	}

	if (host.empty() || user.empty())
	{
		usage(argv[0]);
		return 1;
	}
	if (!ev_default_loop (EVBACKEND_POLL | EVFLAG_NOENV)) {
		fprintf(stderr,"could not initialise libev");
		exit(EXIT_FAILURE);
	}

/*
	int rc;
	rc=pipe(pipefd);
	//ioctl_FIONBIO(pipefd[0],1);
	//ioctl_FIONBIO(pipefd[1],1);
*/
	slave::MasterInfo masterinfo;

	masterinfo.conn_options.mysql_host = host;
	masterinfo.conn_options.mysql_port = port;
	masterinfo.conn_options.mysql_user = user;
	masterinfo.conn_options.mysql_pass = password;
	//signal(SIGINT, sighandler);
	//signal(SIGTERM, sighandler);

	bool error = false;

	try {

		slave::DefaultExtState sDefExtState;
		slave::Slave slave(masterinfo, sDefExtState);
		sl = &slave;

		slave.setXidCallback(bench_xid_callback);

		std::cout << "Initializing client..." << std::endl;
		slave.init();
		// enable GTID
		slave.enableGtid();

		curpos = slave.getLastBinlogPos();
		std::string s1 = gtid_executed_to_string(curpos);
		std::cout << s1 << std::endl;

		sDefExtState.setMasterPosition(curpos);

	pthread_t thread_id;
	//pthread_create(&thread_id, NULL, pipe_reader , NULL);
	pthread_create(&thread_id, NULL, server , NULL);

/*
	for (int i=0; i<NGTIDS; i++) {
		std::string s = "8d07230645be11e78da284ef182005d5";
		uint64_t id=i;
		char buf[128];
		sprintf(buf,"%s",s.c_str());
		memcpy(buf+32,&id,sizeof(id));
		write(pipefd[1],buf,40);
	}
*/
		try {

			std::cout << "Reading binlogs..." << std::endl;
			slave.get_remote_binlog([&] ()
				{
					const slave::MasterInfo& sMasterInfo = slave.masterInfo();
					return (isStopping());
				});


		} catch (std::exception& ex) {
			std::cout << "Error in reading binlogs: " << ex.what() << std::endl;
			error = true;
		}

		pthread_join(thread_id, NULL);
	} catch (std::exception& ex) {
		std::cout << "Error in initializing slave: " << ex.what() << std::endl;
		error = true;
	}
	return 0;
}
