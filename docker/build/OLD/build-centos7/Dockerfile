FROM proxysql/packaging:build-centos7-v2.5.3

LABEL authors="Miro Stauder <miro@proxysql.com>"

# fix repos
RUN sed -i 's/mirrorlist/#mirrorlist/' /etc/yum.repos.d/CentOS-*.repo
RUN sed -i 's/#baseurl/baseurl/' /etc/yum.repos.d/CentOS-*.repo
RUN sed -i 's/mirror./vault./' /etc/yum.repos.d/CentOS-*.repo

# dependencies & tools
RUN yum update -y && \
	yum install -y \
	nss curl libcurl libtool bison \
	boost boost-devel \
	libffi-devel readline-devel sqlite-devel

# install epel
RUN yum install -y epel-release && \
  rpm --import /etc/pki/rpm-gpg/RPM-GPG-KEY-EPEL-7

# set git safe dir
RUN git config --system --add safe.directory /opt/

# mysql 5.7 libs
RUN wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64/mysql-community-devel-5.7.28-1.el7.x86_64.rpm && \
	wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64/mysql-community-libs-5.7.28-1.el7.x86_64.rpm && \
	wget --quiet http://repo.mysql.com/yum/mysql-5.7-community/el/7/x86_64/mysql-community-common-5.7.28-1.el7.x86_64.rpm && \
	rpm -ihv mysql-community-common-5.7.28-1.el7.x86_64.rpm mysql-community-devel-5.7.28-1.el7.x86_64.rpm mysql-community-libs-5.7.28-1.el7.x86_64.rpm && \
	wget -q -O /usr/include/mysql/hash.h https://raw.githubusercontent.com/mysql/mysql-server/5.7/include/hash.h

# fpm packaging
RUN curl -sSL https://rvm.io/mpapis.asc | gpg2 --import -
RUN curl -sSL https://rvm.io/pkuczynski.asc | gpg2 --import -
RUN curl -sSL https://get.rvm.io | bash -s stable
SHELL [ "/bin/bash", "-c" ]
RUN source /etc/profile.d/rvm.sh && rvm install ruby 3.0.2 && rvm --default use ruby-3.0.2 && gem install fpm

# cleanup rpm cache
RUN yum clean all && \
	rm -rf /var/cache/yum

ENTRYPOINT [ "bash", "-c", "source /etc/profile.d/rvm.sh && /opt/entrypoint/entrypoint.bash" ]
