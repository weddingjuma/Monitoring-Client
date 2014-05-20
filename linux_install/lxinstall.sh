#!/bin/bash
 
export TEMPSELECT=/tmp/choice
export OUTPUT=/tmp/out

function default_server {
	# clear terminal
	clear
	
	# create directories
	mkdir /opt/monitoring
	mkdir /opt/monitoring/config
	mkdir /opt/monitoring/data
	mkdir /var/log/monitoring
	
	# copy boost files
	cp boost/ /usr/include/ -r
	cp threadpool.hpp /usr/include/boost/
	cp threadpool/ /usr/include/boost/ -r
	cp mysql++/ /usr/include/ -r
	
	# compile
	g++ -o monitoring-server main.cpp -lpthread -lboost_system -lboost_regex -lboost_thread -lmysqlpp -I/usr/include/mysql
	
	# copy server files
	cp monitoring-server /opt/monitoring/
	cp default.cfg /opt/monitoring/config
	cp masterlist.txt /opt/monitoring/config
	cp mserver /etc/init.d/
	
	# build database TODO:::
	dialog --inputbox "Enter address of your MySQL server:" 30 80 2>$TEMPSELECT
	OUT=`cat $TEMPSELECT`
	dialog --inputbox "Enter the username with database create rights on your MySQL server:" 30 80 2>$TEMPSELECT
	USER=`cat $TEMPSELECT`
	dialog --inputbox "Enter the password for your MySQL user:" 30 80 2>$TEMPSELECT
	PASS=`cat $TEMPSELECT`
	dialog --inputbox "Enter a name for your database:" 30 80 2>$TEMPSELECT
	NAME=`cat $TEMPSELECT`
	mysqladmin -h \'$OUT\' create $NAME -u $USER -p $PASS
	gunzip < monitoring-server-db.gz | mysql $NAME
	
	# inform user they need to manually add init script to default runlevel based on their distro method
	dialog --msgbox \
		"Installation has completed.  A manual add of the init script to default runlevel is required in order for the server to perform as	a service\n\nYou should also edit the server configuration file to suit your needs located in /opt/monitoring/config/default.cfg" 30 80
}

function custom_server {
	clear
	
	# Create directories
	dialog --inputbox "Type the location you want the Server installed to:" 18 60 2>$TEMPSELECT
	OUT=`cat $TEMPSELECT`
	mkdir $OUT
	mkdir $OUT/monitoring
	mkdir $OUT/monitoring/config
	mkdir $OUT/monitoring/data
	mkdir /var/log/monitoring	
	
	# Copy Boost and Mysql++ files
	cp boost/ /usr/include/ -r
	cp threadpool.hpp /usr/include/boost/
	cp threadpool/ /usr/include/boost/ -r
	cp mysql++/ /usr/include/ -r
	
	# Compile 
	g++ -o monitoring-server main.cpp -lpthread -lboost_system -lboost_regex -lboost_thread -lmysqlpp -I/usr/include/mysql
	
	# Copy server files
	cp monitoring-server $OUT/monitoring/
	cp default.cfg $OUT/monitoring/config/
	cp masterlist.txt $OUT/monitoring/config/
	cp mserver /etc/init.d/
	
	# Build database TODO:::
	
	# Inform the user they need to manually add the init script to their default runlevel based on their distro installation method
	dialog --msgbox \
		"Installation has complted.  A manual add of the init script to default runlevel is required in order for the server to perform as a service.\n\nYou should also edit the server configuration file to suit your needs located in $OUT/monitoring/config/default.cfg" 30 80
}

function default_client {
	clear
	
	# Create directories
	mkdir /opt/monitoring
	mkdir /opt/monitoring/config
	mkdir /opt/monitoring/data
	mkdir /var/log/monitoring/
	
	# Copy Boost files
	cp boost/ /usr/include/ -r
	cp threadpool.hpp /usr/include/boost/
	cp threadpool/ /usr/include/boost/ -r
	
	# Compile
	g++ -o monitoring-client main.cpp -lpthread -lboost_system -lboost_regex -lboost_thread -lboost_threadpool
	
	# Copy client files
	cp monitoring-client /opt/monitoring/
	cp default.cfg /opt/monitoring/config/
	cp masterlist.txt /opt/monitoring/config/
	cp mclient /etc/init.d/
	
	# Compile reader
	g++  -o mclient_reader main.cpp linux.cpp -lpthread
	
	# Copy reader
	cp mclient_reader /opt/monitoring/
	cp mclient_reader.desktop 
	
	dialog --msgbox \
		"Installation has completed.  A manual add of the init script to default runlevel is required in order for the client to perform as a service.\n\nYou should also edit the client configuration file to suit your needs located in /opt/monitoring/config/default.cfg" 30 80
	
}

function custom_client {
	clear
	
	# Create custom directories
	dialog --inputbox "Type the location you want the Client installed to:" 18 60 2>$TEMPSELECT
	OUT=`cat $TEMPSELECT`
	mkdir $OUT
	mkdir $OUT/monitoring
	mkdir $OUT/monitoring/config
	mkdir $OUT/monitoring/data
	mkdir /var/log/monitoring/
	
	# Copy Boost files
	cp boost/ /usr/include/ -r
	cp threadpool.hpp /usr/include/boost/
	cp threadpool/ /usr/include/boost/ -r

	# Compile
	g++ -o monitoring-client main.cpp -lpthread -lboost_system -lboost_regex -lboost_thread -lboost_threadpool
	
	# Copy client files
	
	
}

# Check that the installation is being performed with root privleges otherwise directory creation, which is required,
# will fail.
if [[ $EUID -ne 0 ]]; then
	echo "This installation must be run as root" 1>&2
	exit 1
fi

# Ask if Server or Client install
dialog --clear --msgbox \
	"Welcome to the Monitoring-Client Linux Server or Client install.  Custom installations will allow you to choose your own installation directories.  If you choose a default install the directory structure will be created for you.\n\nIn the case of a Server installation a MySQL database will be created for you on the server address you enter. In which case a valid username and password for either remote or local access (and the necessary privleges) depending on if you are installing to a remote server is required.\n\nMySQL is required for Monitoring-Client Server to run, make sure it is installed.  Boost 1.55.0 libraries and MySQL++ will be installed on this system.\n\nPress ENTER to start installation.\n" 30 80
	
dialog --clear --menu "Monitoring-Client Linux Installation Main Menu" \
		30 80 5 \
		defaultserver "Default Server Installation" \
		customserver "Custom Server Installation" \
		defaultclient "Default Client Installation" \
		customclient "Custom Client Installation" \
		quit "Quit" 2>$TEMPSELECT
SELECTION=`cat $TEMPSELECT`

case $SELECTION
in
defaultserver ) default_server; ;;
customserver ) custom_server; ;;
defaultclient ) ;;
customclient ) ;;
quit ) clear; exit ;;
esac
clear
echo $SELECTION
