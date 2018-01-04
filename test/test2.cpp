#include <unistd.h>
#include <iostream>
#include <sstream>
#include <signal.h>

#include "Slave.h"
#include "DefaultExtState.h"


volatile sig_atomic_t stopflag = 0;
slave::Slave* sl = NULL;

slave::Position curpos;
pthread_mutex_t pos_mutex;

std::string gtid_executed_to_string(slave::Position &curpos);

void bench_xid_callback(unsigned int server_id) {
	pthread_mutex_lock(&pos_mutex);
	std::cout << sl->gtid_next.first << ":" << sl->gtid_next.second << std::endl;
	curpos.addGtid(sl->gtid_next);
	pthread_mutex_unlock(&pos_mutex);
}

void sighandler(int sig) {
	stopflag = 1;
	sl->close_connection();
	std::cout << " Received signal. Stopping at:" << std::endl;
	std::string s1 = gtid_executed_to_string(curpos);
	std::cout << s1 << std::endl;
}

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


int main(int argc, char** argv) {
	std::string host;
	std::string user;
	std::string password;
	std::string database;
	unsigned int port = 3306;

	pthread_mutex_init(&pos_mutex, NULL);

	int c;
	while (-1 != (c = ::getopt(argc, argv, "h:u:p:P:"))) {   
		switch (c) {
			case 'h': host = optarg; break;
			case 'u': user = optarg; break;
			case 'p': password = optarg; break;
			case 'P': port = std::stoi(optarg); break;
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

	slave::MasterInfo masterinfo;

	masterinfo.conn_options.mysql_host = host;
	masterinfo.conn_options.mysql_port = port;
	masterinfo.conn_options.mysql_user = user;
	masterinfo.conn_options.mysql_pass = password;
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

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

	} catch (std::exception& ex) {
		std::cout << "Error in initializing slave: " << ex.what() << std::endl;
		error = true;
	}
	return 0;
}
