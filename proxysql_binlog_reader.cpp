#include <sstream>
#include <signal.h>

#include <assert.h>
#include <ev.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <iostream>
#include <vector>
#include <algorithm>

#include <libdaemon/dfork.h>
#include <libdaemon/dsignal.h>
#include <libdaemon/dlog.h>
#include <libdaemon/dpid.h>
#include <libdaemon/dexec.h>


#include "Slave.h"
#include "DefaultExtState.h"

#define BINLOG_VERSION GITVERSION

#define ioctl_FIONBIO(fd, mode) \
{ \
  int ioctl_mode=mode; \
  ioctl(fd, FIONBIO, (char *)&ioctl_mode); \
}

void proxy_error_func(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
};

#define proxy_info(fmt, ...) \
	do { \
		time_t __timer; \
		char __buffer[25]; \
		struct tm *__tm_info; \
		time(&__timer); \
		__tm_info = localtime(&__timer); \
		strftime(__buffer, 25, "%Y-%m-%d %H:%M:%S", __tm_info); \
		proxy_error_func("%s [INFO] " fmt , __buffer , ## __VA_ARGS__); \
	} while(0)

#define DEFAULT_ERRORLOG     "/tmp/proxysql_mysqlbinlog.log"
#define DEFAULT_MYSQL_PORT   3306
#define DEFAULT_LISTEN_PORT  6020
#define NETBUFLEN            256
#define WRITE_CHUNKLEN       4096

struct ev_async async;
std::vector<struct ev_io *> Clients;

pid_t pid;
time_t laststart;
pthread_mutex_t pos_mutex;
std::vector<char *> server_uuids;
std::vector<uint64_t> trx_ids;

std::string gtid_executed_to_string(slave::Position &curpos);

static struct ev_loop *loop;

volatile sig_atomic_t stopflag = 0;
slave::Slave* sl = NULL;

slave::Position curpos;

int pipefd[2];

char last_server_uuid[256];
uint64_t last_trx_id = 0;

// Global arguments
char *errorlog = NULL;
bool foreground = false;
unsigned int listen_port = DEFAULT_LISTEN_PORT;
uint64_t update_freq_ms = 0;

static const char * proxysql_binlog_pid_file() {
	static char fn[512];
	snprintf(fn, sizeof(fn), "%s", daemon_pid_file_ident);
	return fn;
}

void flush_error_log() {
	if (foreground==false) {
		int outfd=0;
		int errfd=0;
		outfd=open(errorlog, O_WRONLY | O_APPEND | O_CREAT , S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
		if (outfd>0) {
			dup2(outfd, STDOUT_FILENO);
			close(outfd);
		} else {
			fprintf(stderr,"Impossible to open file\n");
		}
		errfd=open(errorlog, O_WRONLY | O_APPEND | O_CREAT , S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
		if (errfd>0) {
			dup2(errfd, STDERR_FILENO);
			close(errfd);
		} else {
			fprintf(stderr,"Impossible to open file\n");
		}
	}
}


void daemonize_wait_daemon() {
	int ret;
	if ((ret = daemon_retval_wait(2)) < 0) {
		daemon_log(LOG_ERR, "Could not receive return value from daemon process: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (ret) {
		daemon_log(LOG_ERR, "Daemon returned %i as return value.", ret);
	}
	exit(ret);
}


bool daemonize_phase2() {
	int rc;
	/* Close FDs */
	if (daemon_close_all(-1) < 0) {
		daemon_log(LOG_ERR, "Failed to close all file descriptors: %s", strerror(errno));
		/* Send the error condition to the parent process */
		daemon_retval_send(1);
		return false;
	}

	rc=chdir("/tmp");
	if (rc) {
		daemon_log(LOG_ERR, "Could not chdir into datadir: %s . Error: %s", "/tmp", strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Create the PID file */
	if (daemon_pid_file_create() < 0) {
		daemon_log(LOG_ERR, "Could not create PID file (%s).", strerror(errno));
		daemon_retval_send(2);
		return false;
	}

	/* Send OK to parent process */
	daemon_retval_send(0);
	flush_error_log();
	fprintf(stderr,"Starting ProxySQL MySQL Binlog\n");
	fprintf(stderr,"Sucessfully started\n");

	return true;
}

void parent_open_error_log() {
	if (foreground==false) {
		int outfd=0;
		int errfd=0;
		outfd=open(errorlog, O_WRONLY | O_APPEND | O_CREAT , S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
		if (outfd>0) {
			dup2(outfd, STDOUT_FILENO);
			close(outfd);
		} else {
			fprintf(stderr,"Impossible to open file\n");
		}
		errfd=open(errorlog, O_WRONLY | O_APPEND | O_CREAT , S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
		if (errfd>0) {
			dup2(errfd, STDERR_FILENO);
			close(errfd);
		} else {
			fprintf(stderr,"Impossible to open file\n");
		}
	}
}


void parent_close_error_log() {
	if (foreground==false) {
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
	}
}


bool daemonize_phase3() {
	int rc;
	int status;
	//daemon_log(LOG_INFO, "Angel process started ProxySQL process %d\n", pid);
	parent_open_error_log();
	fprintf(stderr,"Angel process started ProxySQL MySQL Binlog process %d\n", pid);
	parent_close_error_log();
	rc=waitpid(pid, &status, 0);
	if (rc==-1) {
		parent_open_error_log();
		perror("waitpid");
		//proxy_error("[FATAL]: waitpid: %s\n", perror("waitpid"));
		exit(EXIT_FAILURE);
	}
	rc=WIFEXITED(status);
	if (rc) { // client exit()ed
		rc=WEXITSTATUS(status);
		if (rc==0) {
			//daemon_log(LOG_INFO, "Shutdown angel process\n");
			parent_open_error_log();
			fprintf(stderr,"Shutdown angel process\n");
			exit(EXIT_SUCCESS);
		} else {
			//daemon_log(LOG_INFO, "ProxySQL exited with code %d . Restarting!\n", rc);
			parent_open_error_log();
			fprintf(stderr,"ProxySQL exited with code %d . Restarting!\n", rc);
			parent_close_error_log();
			return false;
		}
	} else {
		parent_open_error_log();
		fprintf(stderr,"ProxySQL crashed. Restarting!\n");
		parent_close_error_log();
		return false;
	}
	return true;
}

void daemonize_phase1(char *argv0) {
	int rc;
	daemon_pid_file_ident="/tmp/proxysql_mysqlbinlog.pid";
	daemon_log_ident=daemon_ident_from_argv0(argv0);
	rc=chdir("/tmp");
	if (rc) {
		daemon_log(LOG_ERR, "Could not chdir into datadir: %s . Error: %s", "/tmp", strerror(errno));
		exit(EXIT_FAILURE);
	}
	daemon_pid_file_proc=proxysql_binlog_pid_file;
	pid=daemon_pid_file_is_running();
	if (pid>=0) {
		daemon_log(LOG_ERR, "Daemon already running on PID file %u", pid);
		exit(EXIT_FAILURE);
	}
	if (daemon_retval_init() < 0) {
		daemon_log(LOG_ERR, "Failed to create pipe.");
		exit(EXIT_FAILURE);
	}
}



class Client_Data {
	public:
	char *data;
	size_t len;
	size_t max_len;
	size_t size;
	size_t pos;
	struct ev_io *w;
	char uuid_server[64];
	char *ip = NULL;
	Client_Data(struct ev_io *_w) {
		w = _w;
		size = NETBUFLEN;
		data = (char *)malloc(size);
		uuid_server[0] = 0;
		pos = 0;
		len = 0;
		max_len = 0;
		ip = strdup("unknown");
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
			// Round up size to n-times NETBUFLEN
			size_t new_s = len + _s;
			new_s = ((new_s / NETBUFLEN) + (new_s % NETBUFLEN != 0 ? 1 : 0)) * NETBUFLEN;
			resize(new_s);
		}
		memcpy(data+len,_ptr,_s);
		len += _s;
		if (len > max_len) max_len = len;
	}
	void add_string(std::string s) {
		add_string(s.c_str(), s.size());
	}
	~Client_Data() {
		if (ip) free(ip);
		free(data);
	}
	void set_ip(char *a,int p) {
		if (ip) free(ip);
		ip = (char *)malloc(strlen(a)+16);
		sprintf(ip,"%s:%d",a,p);
	}

	bool writeout() {
		bool ret = true;
		while (len) {
			size_t chunk = len-pos;
			if (chunk > WRITE_CHUNKLEN) { chunk = WRITE_CHUNKLEN; }
			int rc = write(w->fd,data+pos,chunk);
			if (rc > 0) {
				pos += rc;
				if (pos >= len/2) {
					memmove(data,data+pos,len-pos);
					len -= pos;
					pos = 0;
				}
			} else {
				int myerr = errno;
				if (
					(rc==0) ||
					(rc==-1 && myerr != EINTR && myerr != EAGAIN)
				) {
					proxy_info("failed to write %d/%d bytes to client FD %d, error %d", chunk, len-pos, w->fd, errno);
					ret = false;
					break;
				}
			}
		}

		int new_events = EV_READ;
		if (len) {
			new_events |= EV_WRITE;
		}
		if (new_events != w->events) {
			ev_io_stop(loop, w);
			ev_io_set(w, w->fd, new_events);
			ev_io_start(loop, w);
		}

		if (ret == false) {
			ev_io_stop(loop,w);
			shutdown(w->fd,SHUT_RDWR);
			close(w->fd);
		}
		return ret;
	}
};

void read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) {
	std::vector<struct ev_io *>::iterator it;
	it = std::find(Clients.begin(), Clients.end(), watcher);
	if (it != Clients.end()) {
		//proxy_info("Remove client with FD %d\n", watcher->fd);
		Clients.erase(it);
	}
	if(EV_ERROR & revents) {
		perror("got invalid event");
		ev_io_stop(loop,watcher);
		shutdown(watcher->fd,SHUT_RDWR);
		close(watcher->fd);
		Client_Data *custom_data = (Client_Data *)watcher->data;
		delete custom_data;
		watcher->data = NULL;
		free(watcher);
		return;
	}
	ev_io_stop(loop,watcher);
	shutdown(watcher->fd,SHUT_RDWR);
	close(watcher->fd);
	Client_Data *custom_data = (Client_Data *)watcher->data;
	delete custom_data;
	watcher->data = NULL;
	free(watcher);
}

void accept_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) {
	typedef union { 
		struct sockaddr_in in;
		struct sockaddr_in6 in6;
	} custom_sockaddr;
	custom_sockaddr client_addr;
	memset(&client_addr, 0, sizeof(custom_sockaddr));
	socklen_t client_len = sizeof(custom_sockaddr);
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
	ioctl_FIONBIO(client_sd,1);
	Client_Data * custom_data = new Client_Data(client);
	struct sockaddr *addr = (struct sockaddr *)&client_addr;
	switch (addr->sa_family) {
		case AF_INET: {
			struct sockaddr_in *ipv4 = (struct sockaddr_in *)&client_addr;
			char buf[INET_ADDRSTRLEN];
			inet_ntop(addr->sa_family, &ipv4->sin_addr, buf, INET_ADDRSTRLEN);
			custom_data->set_ip(buf, ipv4->sin_port);
			break;
		}
		case AF_INET6: {
			struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&client_addr;
			char buf[INET6_ADDRSTRLEN];
			inet_ntop(addr->sa_family, &ipv6->sin6_addr, buf, INET6_ADDRSTRLEN);
			custom_data->set_ip(buf, ipv6->sin6_port);
			break;
		}
	}
	client->data = (void *)custom_data;
	ev_io_init(client, read_cb, client_sd, EV_READ);
	ev_io_start(loop, client);
	pthread_mutex_lock(&pos_mutex);
	std::string s1 = gtid_executed_to_string(curpos);
	pthread_mutex_unlock(&pos_mutex);
	custom_data->add_string("ST=" + s1 + "\n");
	bool ret = custom_data->writeout();
	if (ret) {
		//proxy_info("Adding client with FD %d\n", client->fd);
		Clients.push_back(client);
	} else {
		proxy_info("Error accepting client with FD %d\n", client->fd);
		delete custom_data;
		free(client);
	}
}

void write_clients() {
	pthread_mutex_lock(&pos_mutex);
	std::vector<struct ev_io *> to_remove;
	for (std::vector<struct ev_io *>::iterator it=Clients.begin(); it!=Clients.end(); ++it) {
		struct ev_io *w = *it;
		Client_Data * custom_data = (Client_Data *)w->data;
		for (std::vector<char *>::size_type i=0; i<server_uuids.size(); i++) {
			if (custom_data->uuid_server[0]==0 || strncmp(custom_data->uuid_server, server_uuids.at(i), strlen(server_uuids.at(i)))) {
				strcpy(custom_data->uuid_server,server_uuids.at(i));
				custom_data->add_string("I1=" + std::string(server_uuids.at(i)) + ":" + std::to_string(trx_ids.at(i)) + "\n");
			} else {
				custom_data->add_string("I2=" + std::to_string(trx_ids.at(i)) + "\n");
			}
		}
		if (!custom_data->writeout()) {
			delete custom_data;
			to_remove.push_back(w);
		} else {
			// Close connection if we write on every GTID update and the queue grows too big.
			if (!update_freq_ms && custom_data->max_len > 8 * NETBUFLEN) {
				ev_io_stop(loop,w);
				shutdown(w->fd,SHUT_RDWR);
				close(w->fd);
				delete custom_data;
				to_remove.push_back(w);
			}
		}
	}
	for (std::vector<struct ev_io *>::iterator it=to_remove.begin(); it!=to_remove.end(); ++it) {
		struct ev_io *w = *it;
		std::vector<struct ev_io *>::iterator it2 = find(Clients.begin(), Clients.end(), w);
		if (it2 != Clients.end()) {
			Clients.erase(it2);
			free(w);
		}
	}
	for (std::vector<char *>::size_type i=0; i<server_uuids.size(); i++) {
		free(server_uuids.at(i));
	}
	server_uuids.clear();
	trx_ids.clear();

	pthread_mutex_unlock(&pos_mutex);
	return;
}

void async_cb(struct ev_loop *loop, struct ev_async *watcher, int revents) {
	write_clients();
	return;
}

void timer_cb(struct ev_loop *loop, struct ev_timer *t, int revents) {
	write_clients();
	return;
}

static void sigint_cb (struct ev_loop *loop, ev_signal *w, int revents) {
	stopflag = 1;
	sl->close_connection();
	//std::cout << " Received signal. Stopping at:" << std::endl;
	std::string s1 = gtid_executed_to_string(curpos);
	//std::cout << s1 << std::endl;
	proxy_info("Received signal. Stopping at: %s\n", s1.c_str());
	ev_break(loop, EVBREAK_ALL);
}

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
		listen(sd,30);
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
		if (update_freq_ms) {
			proxy_info("Pushing updates every %lums\n", update_freq_ms);
			ev_timer_init(&timer, timer_cb, update_freq_ms / 1000.0, update_freq_ms / 1000.0);
			ev_timer_start(my_loop, &timer);
		} else {
			ev_async_init(&async, async_cb);
			ev_async_start(my_loop, &async);
		}
		ev_signal signal_watcher1;
		ev_signal signal_watcher2;
		ev_signal_init (&signal_watcher1, sigint_cb, SIGINT);
		ev_signal_init (&signal_watcher2, sigint_cb, SIGTERM);
		ev_signal_start (loop, &signal_watcher1);
		ev_signal_start (loop, &signal_watcher2);
		ev_run(my_loop, 0);
	}
	~GTID_Server_Dumper() {
		close(sd);
	}
};



void bench_xid_callback(unsigned int server_id) {
	pthread_mutex_lock(&pos_mutex);

	const char *uuid=sl->gtid_next.first.c_str();
	uint64_t trx_id = sl->gtid_next.second;
	if (last_trx_id == trx_id && strcmp(last_server_uuid, uuid)) {
		// do nothing
		pthread_mutex_unlock(&pos_mutex);
		return;
	}

	strcpy(last_server_uuid, uuid);
	last_trx_id = trx_id;
	server_uuids.push_back(strdup(uuid));
	trx_ids.push_back(trx_id);
	curpos.addGtid(sl->gtid_next);
	pthread_mutex_unlock(&pos_mutex);
	if (!update_freq_ms) {
		ev_async_send(loop, &async);
	}
}

bool isStopping() {
	return stopflag;
}

std::string gtid_executed_to_string(slave::Position &curpos) {
	std::string gtid_set { "" };
	for (auto it=curpos.gtid_executed.begin(); it!=curpos.gtid_executed.end(); ++it) {
		std::string s = it->first;
		s.insert(8,"-");
		s.insert(13,"-");
		s.insert(18,"-");
		s.insert(23,"-");
		s = s + ":";
		for (auto itr = it->second.begin(); itr != it->second.end(); ++itr) {
			std::string s2 = s;
			s2 = s2 + std::to_string(itr->first);
			s2 = s2 + "-";
			s2 = s2 + std::to_string(itr->second);
			s2 = s2 + ",";
			gtid_set = gtid_set + s2;
		}
	}
	if (gtid_set.empty() == false) {
		gtid_set.pop_back();
	}
	return gtid_set;
}

void usage(const char* name) {
	std::cout << "Usage: " << name << " [args]\n"
	"\n"
	"Required arguments:\n"
	"\n"
	"-h: MySQL host address.\n"
	"-u: MySQL user.\n"
	"\n"
	"Optional arguments:\n"
	"\n"
	"-L: Log file path (default " DEFAULT_ERRORLOG ").\n"
	"-P: MySQL port (default " << DEFAULT_MYSQL_PORT << ").\n"
	"-p: MySQL password.\n"
	"-l: Listener port (default " << DEFAULT_LISTEN_PORT << ").\n"
	"-t: Update freqency, in milliseconds. Default is update on every event (0).\n"
	"-f: Run in foreground.\n"
	"-v: Outputs build version.\n"
	<< std::endl;
}

void * server(void *args) {
	GTID_Server_Dumper * serv_dump = new GTID_Server_Dumper(listen_port);
	return NULL;
}

int main(int argc, char** argv) {
	std::string host;
	std::string user;
	std::string password;
	std::string errorstr;
	unsigned int port = DEFAULT_MYSQL_PORT;

	bool error = false;

	int c;
	while (-1 != (c = ::getopt(argc, argv, "vft:h:u:p:P:l:L:"))) {
		switch (c) {
			case 'f': foreground=true; break;
			case 'h': host = optarg; break;
			case 'u': user = optarg; break;
			case 'p':
				password = optarg;
				memset(optarg,'x',strlen(optarg));
				break;
			case 'P': port = std::stoi(optarg); break;
			case 'l': listen_port = std::stoi(optarg); break;
			case 'L': errorstr = optarg; break;
			case 't': update_freq_ms = std::stoi(optarg); break;
			case 'v':
				std::cout << "proxysql_binlog_reader version " << BINLOG_VERSION << std::endl;
				return 1;
			default:
				usage(argv[0]);
				return 1;
		}
	}

	if (errorstr.empty()) {
		errorlog = (char *)DEFAULT_ERRORLOG;
	} else {
		errorlog = strdup(errorstr.c_str());
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


	if (foreground==false) {
	daemonize_phase1((char *)argv[0]);
	if ((pid = daemon_fork()) < 0) {
			/* Exit on error */
			daemon_retval_done();
			exit(EXIT_FAILURE);

		} else if (pid) { /* The parent */

			daemonize_wait_daemon();

		} else {
			if (daemonize_phase2()==false) {
				goto finish;
			}

		}


	laststart=0;
	if (true) {
gotofork:
		if (laststart) {
			int currenttime=time(NULL);
			if (currenttime == laststart) { /// we do not want to restart multiple times in the same second
				// if restart is too frequent, something really bad is going on
				parent_open_error_log();
				fprintf(stderr,"Angel process is waiting %d seconds before starting a new process\n", 1);
				parent_close_error_log();
				sleep(1);
			}
		}
		laststart=time(NULL);
		pid = fork();
		if (pid < 0) {
			parent_open_error_log();
			fprintf(stderr,"[FATAL]: Error in fork()\n");
			exit(EXIT_FAILURE);
		}

		if (pid) { /* The parent */

			parent_close_error_log();
			if (daemonize_phase3()==false) {
				goto gotofork;
			}

		} else { /* The daemon */
			// we open the files also on the child process
			// this is required if the child process was created after a crash
			parent_open_error_log();
		}
	}



	} else {
	   flush_error_log();
	}

__start_label:

{
	pthread_mutex_init(&pos_mutex, NULL);

	slave::MasterInfo masterinfo;

	masterinfo.conn_options.mysql_host = host;
	masterinfo.conn_options.mysql_port = port;
	masterinfo.conn_options.mysql_user = user;
	masterinfo.conn_options.mysql_pass = password;

	try {
		proxy_info("proxysql_binlog_reader version %s\n", BINLOG_VERSION);

		slave::DefaultExtState sDefExtState;
		slave::Slave slave(masterinfo, sDefExtState);
		sl = &slave;

		slave.setXidCallback(bench_xid_callback);

		//std::cout << "Initializing client..." << std::endl;
		proxy_info("Initializing client...\n");
		slave.init();
		// enable GTID
		slave.enableGtid();

		curpos = slave.getLastBinlogPos();
		std::string s1 = gtid_executed_to_string(curpos);

		// Wait until a valid 'GTID' has been executed for requesting binlog
		while (s1.empty() && !isStopping()) {
			proxy_info("'Executed_Gtid_Set' found empty, retrying...\n");
			usleep(1000 * 1000);

			curpos = slave.getLastBinlogPos();
			s1 = gtid_executed_to_string(curpos);
		}
		proxy_info("Last executed GTID: '%s'\n", s1.c_str());

		sDefExtState.setMasterPosition(curpos);

	pthread_t thread_id;
	pthread_create(&thread_id, NULL, server , NULL);

		try {

			proxy_info("Reading binlogs...\n");
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
}

finish:
	proxy_info("Exiting...\n");
	daemon_retval_send(255);
	daemon_signal_done();
	daemon_pid_file_remove();
	return 0;
}
