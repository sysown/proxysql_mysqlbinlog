#include <sstream>
#include <signal.h>

#include <atomic>

#include <assert.h>
#include <cerrno>
#include <ev.h>
#include <getopt.h>
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

#include "mariadb_replication_client.h"
#include "proxysql_gtid.h"

#define BINLOG_VERSION GITVERSION

#define ioctl_FIONBIO(fd, mode) \
{ \
  int ioctl_mode=mode; \
  ioctl(fd, FIONBIO, (char *)&ioctl_mode); \
}

void proxy_log_func(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
};

#define proxy_log(level, fmt, ...) \
	do { \
		time_t __timer; \
		char __buffer[25]; \
		struct tm *__tm_info; \
		time(&__timer); \
		__tm_info = localtime(&__timer); \
		strftime(__buffer, 25, "%Y-%m-%d %H:%M:%S", __tm_info); \
		proxy_log_func("%s [" level "] " fmt , __buffer , ## __VA_ARGS__); \
	} while(0);

#define proxy_info(fmt, ...)   proxy_log("INFO", fmt, ## __VA_ARGS__);
#define proxy_error(fmt, ...)  proxy_log("ERROR", fmt, ## __VA_ARGS__);

#define NETBUFLEN                            256
#define WRITE_CHUNKLEN                       4096
#define ERRORLOG_OPEN_FLAGS                  (O_WRONLY | O_APPEND | O_CREAT)
#define ERRORLOG_STAT_FLAGS                  (S_IWUSR | S_IRGRP | S_IWGRP)
#define DEFAULT_ERRORLOG                     "/tmp/proxysql_mysqlbinlog.log"
#define DEFAULT_MYSQL_PORT                   3306
#define DEFAULT_LISTEN_PORT                  6020
#define DEFAULT_HEARTBEAT_PERIOD_SECONDS     5
#define DEFAULT_READ_TIMEOUT_SECONDS         60
#define DEFAULT_MAX_NETBUFLEN_STREAMING      (8 * NETBUFLEN)
#define DEFAULT_MAX_NETBUFLEN_BATCHED        (8192 * NETBUFLEN)
#define PROXYSQL_UPDATE_BATCHING_MIN_VERSION "3.0.8"
#define UUID_SIZE_BYTES                      64

struct ev_async async;
struct ev_async shutdown_async;
std::vector<struct ev_io *> Clients;

pid_t pid;
time_t laststart;
pthread_mutex_t pos_mutex;

std::vector<char *> server_uuids;
std::vector<uint64_t> trx_ids;

static struct ev_loop *loop;

std::atomic<bool> stopflag(false);
std::atomic<bool> server_loop_ready(false);
std::atomic<bool> client_update_ready(false);
MariaDBReplicationClient* replication_client = NULL;
GTID_Set curpos;

int pipefd[2];

char last_server_uuid[256];
uint64_t last_trx_id = 0;

// Global arguments
char *errorlog = NULL;
bool foreground = false;
size_t max_netbuflen = 0;
uint64_t update_freq_ms = 0;
bool update_batching = true;

struct ListenerConfig {
	int family;
	struct sockaddr_storage address;
	socklen_t address_length;
	unsigned int port;
};

ListenerConfig listener_config;

static bool parse_listener_port(const std::string& value, unsigned int* port) {
	if (value.empty())
		return false;

	unsigned long parsed = 0;
	for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
		if (*it < '0' || *it > '9')
			return false;
		const unsigned long digit = static_cast<unsigned long>(*it - '0');
		if (parsed > (65535 - digit) / 10)
			return false;
		parsed = parsed * 10 + digit;
	}

	if (parsed == 0)
		return false;
	*port = static_cast<unsigned int>(parsed);
	return true;
}

static bool parse_listener_config(const char* value, ListenerConfig* config) {
	if (!value || !*value || !config)
		return false;

	const std::string spec(value);
	std::string host;
	std::string port;
	int family = AF_INET;

	if (spec[0] == '[') {
		const std::string::size_type bracket = spec.find(']');
		if (bracket == std::string::npos || bracket == 1 ||
		    bracket + 1 >= spec.size() || spec[bracket + 1] != ':')
			return false;
		host = spec.substr(1, bracket - 1);
		port = spec.substr(bracket + 2);
		family = AF_INET6;
	} else {
		const std::string::size_type colon = spec.find(':');
		if (colon == std::string::npos) {
			port = spec;
		} else {
			if (colon == 0 || colon != spec.rfind(':'))
				return false;
			host = spec.substr(0, colon);
			port = spec.substr(colon + 1);
		}
	}

	unsigned int parsed_port = 0;
	if (!parse_listener_port(port, &parsed_port))
		return false;

	memset(config, 0, sizeof(*config));
	config->family = family;
	config->port = parsed_port;
	if (family == AF_INET) {
		struct sockaddr_in* address =
			reinterpret_cast<struct sockaddr_in*>(&config->address);
		address->sin_family = AF_INET;
		address->sin_port = htons(parsed_port);
		if (host.empty()) {
			address->sin_addr.s_addr = INADDR_ANY;
		} else if (inet_pton(AF_INET, host.c_str(), &address->sin_addr) != 1) {
			return false;
		}
		config->address_length = sizeof(*address);
	} else {
		struct sockaddr_in6* address =
			reinterpret_cast<struct sockaddr_in6*>(&config->address);
		address->sin6_family = AF_INET6;
		address->sin6_port = htons(parsed_port);
		if (inet_pton(AF_INET6, host.c_str(), &address->sin6_addr) != 1)
			return false;
		config->address_length = sizeof(*address);
	}

	return true;
}

static void test_delay_after_snapshot() {
	const char* const value =
		getenv("PROXYSQL_BINLOG_READER_TEST_AFTER_SNAPSHOT_DELAY_MS");
	if (!value || !*value)
		return;

	errno = 0;
	char* end = NULL;
	const unsigned long delay_ms = strtoul(value, &end, 10);
	if (errno == ERANGE || !end || *end != '\0' || delay_ms > 60000) {
		proxy_error("Ignoring invalid post-snapshot test delay: '%s'", value);
		return;
	}

	proxy_info("Applying post-snapshot test delay of %lu ms", delay_ms);
	usleep(static_cast<useconds_t>(delay_ms * 1000));
}

static const char * proxysql_binlog_pid_file() {
	static char fn[512];
	snprintf(fn, sizeof(fn), "%s", daemon_pid_file_ident);
	return fn;
}

void flush_error_log() {
	if (foreground==false) {
		int outfd=0;
		int errfd=0;
		outfd=open(errorlog, ERRORLOG_OPEN_FLAGS, ERRORLOG_STAT_FLAGS);
		if (outfd>0) {
			dup2(outfd, STDOUT_FILENO);
			close(outfd);
		} else {
			fprintf(stderr,"Impossible to open file\n");
		}
		errfd=open(errorlog, ERRORLOG_OPEN_FLAGS, ERRORLOG_STAT_FLAGS);
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
		//proxy_error("[FATAL]: waitpid: %s", perror("waitpid"));
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

std::string position_to_string(GTID_Set& position) {
	return position.to_string();
}

class Client_Data {
	public:
	char *data;
	size_t len;
	size_t max_len;
	size_t size;
	size_t pos;
	struct ev_io *w;
	char uuid_server[UUID_SIZE_BYTES];
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
					proxy_error("failed to write %d/%d bytes to client FD %d, error %d", chunk, len-pos, w->fd, errno);
					ret = false;
					break;
				}
			}
		}

		if (ret) {
			int new_events = EV_READ;
			if (len) {
				new_events |= EV_WRITE;
			}
			if (new_events != w->events) {
				ev_io_stop(loop, w);
				ev_io_set(w, w->fd, new_events);
				ev_io_start(loop, w);
			}
		} else {
			ev_io_stop(loop,w);
			shutdown(w->fd,SHUT_RDWR);
			close(w->fd);
		}
		return ret;
	}
};

void write_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) {
	Client_Data * custom_data = (Client_Data *)watcher->data;
	bool rc = custom_data->writeout();
	if (rc == false) {
		std::vector<struct ev_io *>::iterator it;
		it = std::find(Clients.begin(), Clients.end(), watcher);
		if (it != Clients.end()) {
			Clients.erase(it);
		}
		delete custom_data;
		free(watcher);
	}
}

void read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) {
	std::vector<struct ev_io *>::iterator it;
	it = std::find(Clients.begin(), Clients.end(), watcher);
	if (it != Clients.end()) {
		//proxy_info("Remove client with FD %d", watcher->fd);
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

void io_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) {
	if ((EV_READ & revents) || (EV_ERROR & revents)) {
		read_cb(loop, watcher, revents);
	} else if (EV_WRITE & revents) {
		write_cb(loop, watcher, revents);
	}
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
	ev_io_init(client, io_cb, client_sd, EV_READ);
	ev_io_start(loop, client);
	pthread_mutex_lock(&pos_mutex);
	std::string s1 = position_to_string(curpos);
	pthread_mutex_unlock(&pos_mutex);
	custom_data->add_string("ST=" + s1 + "\n");
	if (custom_data->writeout()) {
		//proxy_info("Adding client with FD %d", client->fd);
		Clients.push_back(client);
	} else {
		proxy_error("Error accepting client with FD %d", client->fd);
		delete custom_data;
		free(client);
	}
}

void write_clients() {
	pthread_mutex_lock(&pos_mutex);

	std::vector<struct ev_io *> to_remove;
	GTID_Set gtid_set;

	for (std::vector<struct ev_io *>::iterator it=Clients.begin(); it!=Clients.end(); ++it) {
		struct ev_io *w = *it;
		Client_Data * custom_data = (Client_Data *)w->data;

		if (!update_batching) {
		    // Generate a I1/I2 message per update per server.
		    for (std::vector<char *>::size_type i=0; i<server_uuids.size(); i++) {
				if (custom_data->uuid_server[0]==0 || strncmp(custom_data->uuid_server, server_uuids.at(i), strlen(server_uuids.at(i)))) {
				    strncpy(custom_data->uuid_server,server_uuids.at(i), UUID_SIZE_BYTES);
					custom_data->add_string("I1=" + std::string(server_uuids.at(i)) + ":" + std::to_string(trx_ids.at(i)) + "\n");
				} else {
				    custom_data->add_string("I2=" + std::to_string(trx_ids.at(i)) + "\n");
				}
			}
	    } else {
	        // Group updates into a single I3/I4 message per server.
			gtid_set.clear();
			for (std::vector<char *>::size_type i=0; i<server_uuids.size(); i++) {
			    gtid_set.add(server_uuids.at(i), trx_ids.at(i));
			}

			for (auto mit = gtid_set.map.begin(); mit != gtid_set.map.end(); mit++) {
			    auto uuid = mit->first;
				auto gtid_sets = mit->second;
				for (auto it = gtid_sets.begin(); it != gtid_sets.end(); it++) {
					if (custom_data->uuid_server[0]==0 || strncmp(custom_data->uuid_server, uuid.c_str(), uuid.size())) {
	                    strncpy(custom_data->uuid_server, uuid.c_str(), UUID_SIZE_BYTES);
					    custom_data->add_string("I3=" + uuid + ":" + it->to_string() + "\n");
					} else {
				        custom_data->add_string("I4=" + it->to_string() + "\n");
					}
				}
			}
		}

		if (!custom_data->writeout()) {
			delete custom_data;
			to_remove.push_back(w);
		} else {
			// Close connection if the write queue grows too big.
			if (custom_data->size > max_netbuflen) {
				proxy_error("network write buffer grew too big (%zu/%zu bytes, max %zu)", custom_data->size, custom_data->max_len, max_netbuflen);
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

void shutdown_async_cb(struct ev_loop *event_loop, struct ev_async *watcher,
	                   int revents) {
	ev_break(event_loop, EVBREAK_ALL);
}

void request_server_shutdown() {
	stopflag.store(true, std::memory_order_seq_cst);
	if (server_loop_ready.load(std::memory_order_seq_cst))
		ev_async_send(loop, &shutdown_async);
}

void timer_cb(struct ev_loop *loop, struct ev_timer *t, int revents) {
	write_clients();
	return;
}

static void sigint_cb (struct ev_loop *loop, ev_signal *w, int revents) {
	stopflag.store(true, std::memory_order_relaxed);
	if (replication_client)
		replication_client->interrupt();
	//std::cout << " Received signal. Stopping at:" << std::endl;
	pthread_mutex_lock(&pos_mutex);
	std::string s1 = position_to_string(curpos);
	pthread_mutex_unlock(&pos_mutex);
	//std::cout << s1 << std::endl;
	proxy_info("Received signal. Stopping at: %s", s1.c_str());
	ev_break(loop, EVBREAK_ALL);
}

class GTID_Server_Dumper {
	private:
	const ListenerConfig& listener;
	int sd;
	struct ev_io ev_accept;
	struct ev_loop *my_loop;
	struct ev_timer timer;
	public:
	GTID_Server_Dumper(const ListenerConfig& _listener) : listener(_listener) {
		sd = socket(listener.family, SOCK_STREAM, 0);
		if (sd < 0) {
			perror("socket");
			exit(EXIT_FAILURE);
		}
		int arg_on = 1;
		if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, (char *)&arg_on, sizeof(arg_on)) == -1) {
			perror("setsocketopt()");
			close(sd);
			exit(EXIT_FAILURE);
		}
		if (listener.family == AF_INET6 &&
		    setsockopt(sd, IPPROTO_IPV6, IPV6_V6ONLY, &arg_on, sizeof(arg_on)) == -1) {
			perror("setsockopt(IPV6_V6ONLY)");
			close(sd);
			exit(EXIT_FAILURE);
		}

		if (bind(sd, reinterpret_cast<const struct sockaddr*>(&listener.address),
		         listener.address_length) != 0) {
			perror("bind");
			exit(EXIT_FAILURE);
		}
		ioctl_FIONBIO(sd,1);
		listen(sd,30);
		//struct ev_loop *my_loop = NULL;
		my_loop = NULL;
		my_loop = ev_loop_new (EVBACKEND_POLL | EVFLAG_NOENV);
		if (my_loop == NULL) {
			fprintf(stderr,"could not initialise new loop");
			exit(EXIT_FAILURE);
		}
		loop = my_loop;
		ev_io_init(&ev_accept, accept_cb, sd, EV_READ);
		ev_io_start(my_loop, &ev_accept);
		ev_async_init(&shutdown_async, shutdown_async_cb);
		ev_async_start(my_loop, &shutdown_async);
		if (update_freq_ms) {
			proxy_info("Pushing %s updates every %lums", update_batching ? "batched" : "non-batched", update_freq_ms);
			ev_timer_init(&timer, timer_cb, update_freq_ms / 1000.0, update_freq_ms / 1000.0);
			ev_timer_start(my_loop, &timer);
		} else {
			ev_async_init(&async, async_cb);
			ev_async_start(my_loop, &async);
			client_update_ready.store(true, std::memory_order_release);
		}
		ev_signal signal_watcher1;
		ev_signal signal_watcher2;
		ev_signal_init (&signal_watcher1, sigint_cb, SIGINT);
		ev_signal_init (&signal_watcher2, sigint_cb, SIGTERM);
		ev_signal_start (loop, &signal_watcher1);
		ev_signal_start (loop, &signal_watcher2);
		server_loop_ready.store(true, std::memory_order_seq_cst);
		if (stopflag.load(std::memory_order_seq_cst))
			ev_async_send(my_loop, &shutdown_async);
		ev_run(my_loop, 0);
		client_update_ready.store(false, std::memory_order_release);
		server_loop_ready.store(false, std::memory_order_release);
	}
	~GTID_Server_Dumper() {
		close(sd);
	}
};

void bench_gtid_callback(const std::string& uuid, uint64_t trx_id) {
	pthread_mutex_lock(&pos_mutex);

	if (last_trx_id == trx_id && uuid == last_server_uuid) {
		// do nothing
		pthread_mutex_unlock(&pos_mutex);
		return;
	}

	strncpy(last_server_uuid, uuid.c_str(), sizeof(last_server_uuid) - 1);
	last_server_uuid[sizeof(last_server_uuid) - 1] = 0;
	last_trx_id = trx_id;
	server_uuids.push_back(strdup(uuid.c_str()));
	trx_ids.push_back(trx_id);
	curpos.add(uuid, trx_id);
	pthread_mutex_unlock(&pos_mutex);
	if (!update_freq_ms && client_update_ready.load(std::memory_order_acquire)) {
		ev_async_send(loop, &async);
	}
}

bool isStopping() {
	return stopflag.load(std::memory_order_relaxed);
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
	"-B: Maximum network buffer size, in bytes (default " << DEFAULT_MAX_NETBUFLEN_STREAMING << ", " << DEFAULT_MAX_NETBUFLEN_BATCHED << " for -t).\n"
	"-L: Log file path (default " << DEFAULT_ERRORLOG << ").\n"
	"-P: MySQL port (default " << DEFAULT_MYSQL_PORT << ").\n"
	"-p: MySQL password.\n"
	"-l: Listener address: PORT (IPv4 wildcard), IPV4:PORT, or [IPV6]:PORT (default " << DEFAULT_LISTEN_PORT << ").\n"
	"-t: Update freqency, in milliseconds. Default is update on every event (0).\n"
	"-b: Batched updates, 0 or 1 (default 1). Requires ProxySQL v" << PROXYSQL_UPDATE_BATCHING_MIN_VERSION << " or later; set to 0 for older versions.\n"
	"-f: Run in foreground.\n"
	"-v: Outputs build version.\n"
	"--ssl-mode: TLS mode: DISABLED, PREFERRED, or REQUIRED (default REQUIRED).\n"
	"--ssl-verify-server-cert: Verify the server certificate, 0 or 1 (default 1).\n"
	"--ssl-ca: CA certificate file.\n"
	"--ssl-capath: CA certificate directory.\n"
	"--ssl-cert: Client certificate file.\n"
	"--ssl-key: Client private-key file.\n"
	"--ssl-cipher: TLS cipher list.\n"
	"--tls-version: TLS protocol version.\n"
	"--heartbeat-period: Replication heartbeat period, in seconds (default " << DEFAULT_HEARTBEAT_PERIOD_SECONDS << ").\n"
	"--read-timeout: Replication read timeout, in seconds (default " << DEFAULT_READ_TIMEOUT_SECONDS << "); must be at least three times the heartbeat period.\n"
	<< std::endl;
}

void * server(void *args) {
	GTID_Server_Dumper serv_dump(listener_config);
	return NULL;
}

int main(int argc, char** argv) {
	std::string host;
	std::string user;
	std::string password;
	std::string errorstr;
	unsigned int port = DEFAULT_MYSQL_PORT;
	TLSOptions tls_options;
	unsigned int heartbeat_period_seconds = DEFAULT_HEARTBEAT_PERIOD_SECONDS;
	unsigned int read_timeout_seconds = DEFAULT_READ_TIMEOUT_SECONDS;
	const std::string default_listener = std::to_string(DEFAULT_LISTEN_PORT);
	if (!parse_listener_config(default_listener.c_str(),
	                           &listener_config)) {
		std::cerr << "cannot initialize default listener" << std::endl;
		return 1;
	}

	enum {
		OPTION_SSL_MODE = 1000,
		OPTION_SSL_VERIFY_SERVER_CERT,
		OPTION_SSL_CA,
		OPTION_SSL_CAPATH,
		OPTION_SSL_CERT,
		OPTION_SSL_KEY,
		OPTION_SSL_CIPHER,
		OPTION_TLS_VERSION,
		OPTION_HEARTBEAT_PERIOD,
		OPTION_READ_TIMEOUT,
	};
	static const struct option long_options[] = {
		{"ssl-mode", required_argument, nullptr, OPTION_SSL_MODE},
		{"ssl-verify-server-cert", required_argument, nullptr,
		 OPTION_SSL_VERIFY_SERVER_CERT},
		{"ssl-ca", required_argument, nullptr, OPTION_SSL_CA},
		{"ssl-capath", required_argument, nullptr, OPTION_SSL_CAPATH},
		{"ssl-cert", required_argument, nullptr, OPTION_SSL_CERT},
		{"ssl-key", required_argument, nullptr, OPTION_SSL_KEY},
		{"ssl-cipher", required_argument, nullptr, OPTION_SSL_CIPHER},
		{"tls-version", required_argument, nullptr, OPTION_TLS_VERSION},
		{"heartbeat-period", required_argument, nullptr, OPTION_HEARTBEAT_PERIOD},
		{"read-timeout", required_argument, nullptr, OPTION_READ_TIMEOUT},
		{nullptr, 0, nullptr, 0},
	};

	bool error = false;

	int c;
	while (-1 != (c = ::getopt_long(argc, argv, "vfB:b:t:h:u:p:P:l:L:",
	                                 long_options, nullptr))) {
		switch (c) {
			case 'B': max_netbuflen = size_t(std::stoi(optarg)); break;
			case 'f': foreground=true; break;
			case 'h': host = optarg; break;
			case 'u': user = optarg; break;
			case 'p':
				password = optarg;
				memset(optarg,'x',strlen(optarg));
				break;
			case 'P': port = std::stoi(optarg); break;
			case 'l':
				if (!parse_listener_config(optarg, &listener_config)) {
					std::cerr << "invalid listener address: " << optarg << std::endl;
					usage(argv[0]);
					return 1;
				}
				break;
			case 'L': errorstr = optarg; break;
			case 't': update_freq_ms = std::stoi(optarg); break;
			case 'b': update_batching = std::stoi(optarg) ? true : false; break;
			case 'v':
				std::cout << "proxysql_binlog_reader version " << BINLOG_VERSION << std::endl;
				return 1;
			case OPTION_SSL_MODE:
				if (!parse_tls_mode(optarg, &tls_options.mode)) {
					std::cerr << "invalid SSL mode" << std::endl;
					usage(argv[0]);
					return 1;
				}
				break;
			case OPTION_SSL_VERIFY_SERVER_CERT:
				if (!parse_tls_boolean(optarg,
				                       &tls_options.verify_server_certificate)) {
					std::cerr << "invalid SSL verification value" << std::endl;
					usage(argv[0]);
					return 1;
				}
				break;
			case OPTION_SSL_CA: tls_options.ca_file = optarg; break;
			case OPTION_SSL_CAPATH: tls_options.ca_path = optarg; break;
			case OPTION_SSL_CERT: tls_options.certificate_file = optarg; break;
			case OPTION_SSL_KEY: tls_options.key_file = optarg; break;
			case OPTION_SSL_CIPHER: tls_options.cipher = optarg; break;
			case OPTION_TLS_VERSION: tls_options.version = optarg; break;
			case OPTION_HEARTBEAT_PERIOD:
				if (!parse_positive_seconds(optarg, &heartbeat_period_seconds)) {
					std::cerr << "invalid replication heartbeat period: " << optarg << std::endl;
					usage(argv[0]);
					return 1;
				}
				break;
			case OPTION_READ_TIMEOUT:
				if (!parse_positive_seconds(optarg, &read_timeout_seconds)) {
					std::cerr << "invalid replication read timeout: " << optarg << std::endl;
					usage(argv[0]);
					return 1;
				}
				break;
			default:
				usage(argv[0]);
				return 1;
		}
	}

	std::string tls_error;
	if (!tls_options_valid(tls_options, &tls_error)) {
		std::cerr << tls_error << std::endl;
		usage(argv[0]);
		return 1;
	}

	if (!validate_replication_timeouts(heartbeat_period_seconds,
	                                   read_timeout_seconds)) {
		std::cerr << "replication read timeout (" << read_timeout_seconds
		          << "s) must be at least three times the heartbeat period ("
		          << heartbeat_period_seconds << "s)" << std::endl;
		usage(argv[0]);
		return 1;
	}

	if (errorstr.empty()) {
		errorlog = (char *)DEFAULT_ERRORLOG;
	} else {
		errorlog = strdup(errorstr.c_str());
	}

	// Disable batching, if frequency is 0.
	if (!update_freq_ms) {
		update_batching = false;
	}

	if (!max_netbuflen) {
		max_netbuflen = size_t(update_freq_ms ? DEFAULT_MAX_NETBUFLEN_STREAMING : DEFAULT_MAX_NETBUFLEN_BATCHED);
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

	MariaDBConnectionOptions connection_options;
	connection_options.host = host;
	connection_options.port = port;
	connection_options.user = user;
	connection_options.password = password;
	connection_options.tls = tls_options;
	connection_options.heartbeat_period_seconds = heartbeat_period_seconds;
	connection_options.read_timeout_seconds = read_timeout_seconds;

	try {
		proxy_info("proxysql_binlog_reader version %s", BINLOG_VERSION);

		MariaDBReplicationClient client(connection_options);
		replication_client = &client;

		//std::cout << "Initializing client..." << std::endl;
		proxy_info("Initializing client...");
		client.connect();

		curpos = client.snapshot();
		std::string s1 = position_to_string(curpos);

		// Wait until a valid 'GTID' has been executed for requesting binlog
		while (s1.empty() && !isStopping()) {
			proxy_info("'Executed_Gtid_Set' found empty, retrying...");
			usleep(1000 * 1000);

			curpos = client.snapshot();
			s1 = position_to_string(curpos);
		}
		proxy_info("Last executed GTID: '%s'", s1.c_str());

		pthread_t thread_id;
		const int thread_error = pthread_create(&thread_id, NULL, server, NULL);
		if (thread_error != 0) {
			proxy_error("Cannot start GTID server thread: %s", strerror(thread_error));
			error = true;
		} else {
			test_delay_after_snapshot();

			try {
				proxy_info("Reading binlogs...");
				client.open_stream();
				client.stream_events(bench_gtid_callback, isStopping);
			} catch (std::exception& ex) {
				std::cout << "Error in reading binlogs: " << ex.what() << std::endl;
				error = true;
				request_server_shutdown();
			}

			pthread_join(thread_id, NULL);
		}
	} catch (std::exception& ex) {
		std::cout << "Error in initializing replication client: " << ex.what() << std::endl;
		error = true;
	}
	replication_client = NULL;
}

finish:
	proxy_info("Exiting...");
	daemon_retval_send(255);
	daemon_signal_done();
	daemon_pid_file_remove();
	return error ? EXIT_FAILURE : EXIT_SUCCESS;
}
