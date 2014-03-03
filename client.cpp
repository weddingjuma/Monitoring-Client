#include <iostream>
#include <string>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <boost/threadpool.hpp>
#include <boost/thread/thread.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <boost/foreach.hpp> // ini parser
#include <cstdio> // for remove()
#include <fstream> // write/read to file
#include <sys/types.h>
#include <sys/stat.h>

#include <boost/program_options/detail/config_file.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/environment_iterator.hpp>
#include <boost/program_options/option.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/detail/config_file.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <signal.h>

#ifdef __linux__
#include <unistd.h>
#endif
#ifdef _WIN32
#include <Windows.h>
#include <TlHelp32.h>
#include <algorithm>
#include <LMCons.h>
#include <LM.h>
#include <WtsApi32.h>
#include <UserEnv.h>
#pragma comment(lib, "Userenv.lib")
#endif

/** Namespaces **/
using boost::asio::ip::tcp;
using boost::property_tree::ptree;
using namespace std;

/** Struct defining logged on users **/
typedef struct{
    std::string name; // User name
    std::string terminal; // Which terminal (ssh, X, etc) the user is logged into
    bool local; // locally logged in (Windows, X) or remotely logged in (SSH, etc)
    time_t started; // timestamp when the user was first detected
} user;

/** The following settings are defaults.  They will read in default values from a config file if it exists or use these.  The user has the option of specifying
a configuration file at runtime that the Client can use instead.  The Server will assume that the default config file will be used and any changes made by the Server
will be reflected in the default.cfg
**/
int EVENTSIZE;
static bool REQUEST_PFILE = false; // set when pfile is empty (initial install) or when some other circumstance has been met to ask the server for a new program file
std::string SERVER_ADDRESS;
int SEND_PORT;
int LISTEN_PORT;
int FREQUENCY; // Frequency to run the gathering portion, a multiple of 60 seconds
int CALL_HOME; // How many minutes to wait with no server contact before attempting to call home
//static std::vector<std::string> USER_NAMES_IGNORED; /** Will store the user names from the config file that will be ignored, like Administrator, etc.. **/

std::string RESTRICTED_MSG; // Account is not allowed to login to this machine
std::string EXPIRE_MSG; // Account nearing expiration for allowed login time
std::string EXPIRED_MSG; // Account has expired and login is no longer permitted
std::vector<int> MSG_DISPLAY_TIMES; // Vector holding a set of minutes telling when to display the EXPIRE message when subtracted from the epiration time (15 min from expiration, 5, 1, etc..)

static std::string BLOCKED_REGEX; // Holds the string of regex patterns for user accounts that will be blocked on this machine
static std::map<std::string, std::vector<std::string> > SCRIPTS; // map Script => Time
static std::map<std::string, time_t> GUEST_EXPIRATION; // store current guest account logged in mapped to unix timestamp of when it was first found
static std::vector<std::string> EVENTS; /** This will hold the currently gathered EVENTS that we'll send to the Server **/
static std::vector<std::string> PROGRAM_LIST; /** This will hold the current program list **/
static int PROGRAM_COUNT = 0; /** This will be the total number of programs currently being monitored, so that we aren't going to disk repeatedly to count **/
static bool LOGGED_IN = false;
static unsigned int *CURRENT_EVENT_BLOCK;
static boost::mutex cu_mutex; /* Mutex */
static boost::mutex pf_mutex; /* PFile mutex */
static std::vector<user> currentUsers; /* Holds the current Users logged into the machine */
static std::map<std::string, int> timeLimitAccounts; /* Holds all accounts being Monitored for a time limit on logins */
static std::vector<std::string> loginRestrictedAccounts; /* Holds all accounts that are restricted from logging into specific machines */
time_t LAST_SERVER_COMMUNICATION;
static bool L_RUNNING = false;
static int ALLOWED = 0x0; /* bit combination representing allowed accounts on this machine, 1 == not allowed, 0 == allowed */ /** Not needed????? **/
boost::mutex event_file_mutex;
std::vector<unsigned char> CURRENT_EVENT;

#ifdef __linux__
char *EVENT_FILE = (char*)"/opt/monitoring/data/events";
char *ERR_LOG = (char*)"/var/log/monitoring-client.log";
char *P_FILE = (char*)"/opt/monitoring/config/masterlist.txt";
char *CONFIG = (char*)"/opt/monitoring/config/default.cfg";
#endif
#ifdef _WIN32
char *EVENT_FILE = (char*)"C:\\Tools\\Monitoring\\data\\events.txt";
char *ERR_LOG = (char*)"C:\\Tools\\Monitoring\\log\\errors.txt";
char *P_FILE = (char*)"C:\\Tools\\Monitoring\\Config\\masterlist.txt";
char *CONFIG = (char*)"C:\\Tools\\Monitoring\\Config\\default.cfg";
#endif

/** Windows Function prototypes go here **/
/*
	Function for the message displaying Thread on Windows clients.  Allows the service to display a message to the interactive user session using named pipes to pass messages to a secondary application
	that is run from the users startup folder.
*/
void display_msgbox();
void display_windows_msgbox();
void display_linux_msgbox();

/*
	Function that gets the current list of users on the system, and if it is not SYSTEM (for Windows), or the user owns pts:0 (for Linux), then returns True
*/
bool logged_in();
bool linux_logged_in();
bool win_logged_in();

/*
	Takes an error message and logs it out to the applications log file
*/
void Log(char *msg);

static std::string get_current_event();
vector<std::string> split_string(std::string s, string delim, bool compress);
void check_allowed_accounts(std::string br);
void resource_cleanup(); /* clean up resources */
void send_local_restricted_message();
void send_remote_restricted_message(std::string pts);
void send_local_time_message();
void send_remote_time_message(std::string pts);
void kick_expired_accounts();
void linux_logoff_user(std::string u);
time_t get_timestamp(); /* Return time_t timestamp for the current time */
bool time_expired(time_t started, int limit);
void write_program_file(std::string list);
void set_program_list();
std::string get_current_local_user();
std::string get_current_remote_user();
void mSleep(int ms); /* Sleep prototype for cross platform compatibility */

/** Program Gathering prototypes **/
std::string linux_get_running_proc(std::string current_user);
int prog_number();
int linux_prog_number(); // Returns the number of programs being monitored
int win_prog_number();
unsigned int tally_program_count(std::string ptree, int block);
unsigned int linux_tally_program_count(std::string ptree, int block); // Returns an integer that is the binary representation of the current blocks program running status
unsigned int win_tally_program_count(std::string ptree, int block);
std::string get_rounded_timestamp();
std::vector<unsigned char> build_event(std::string pdata, std::string user);
void create_directories(); // Create directory and file structure

/**
	KEY - Path (to script)
	VALUE - Int : Int : Int (Weekday : Hour : Minute)
**/
void execute_script(); // Mapping of script to string cron representation of DAY : HOUR : MINUTE
void parse_script(std::string script); // Parse the config file string into a usable script
std::vector<std::string> parse_script2(std::string script, std::string delim);

/*
	Builds an EVENT for this current moment in time.  Used for when the Server contacts the Client and requests program data, but the Client may be just finished with a previous gather
	cycle in which case the data from that previous cycle might not be accurate.
*/
std::string get_current_event() /****** TOOOOOOOOOOOO DOOOOOOOOOOOOOOO.... add remote user stuff into here **************/
{
	std::string EVENT = "";
	std::string pdata;

	if(CURRENT_EVENT_BLOCK == NULL)
		return EVENT;
	else
	{
		int pcount = prog_number();

		int block = 0;
		if(pcount % EVENTSIZE != 0)
			block = (pcount / EVENTSIZE) + 1;
		else
			block = (pcount / EVENTSIZE);


		for(int i = 0; i < block; i++)
		{
			char buffer[32];
			sprintf(buffer, "%08X", CURRENT_EVENT_BLOCK[i]);
			pdata += buffer;
		}
		std::vector<unsigned char> EVENT = build_event(pdata, get_current_local_user());

		/* Store the EVENT in case the Client is unable to send it to the Server */

		std::string line = "";
		for(unsigned i = 0; i < EVENT.size(); i++)
		{
			char c = EVENT[i];
			line = line + c;
		}
		return line;
	}
}

void Log(char *msg)
{
}

void display_msgbox()
{
#ifdef __linux__
    display_linux_msgbox();
#endif // __linux__
#ifdef _WIN32
    display_windows_msgbox();
#endif // _WIN32
}

void display_linux_msgbox()
{
    /// TODO!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
}

void display_windows_msgbox()
{
#ifdef _WIN32
	bool fifteen = false, five = false, one = false, expired = false;

	while(true)
	{
		if(currentUsers.size() > 0)
		{
			//boost::regex gx("^gx", boost::regex::perl|boost::regex::icase);
			boost::regex gp("^gp", boost::regex::perl|boost::regex::icase);
			boost::match_results<std::string::const_iterator> results;

			cu_mutex.lock();
			const std::string user = get_current_local_user();
			cu_mutex.unlock();
			std::string::const_iterator start = user.begin();
			std::string::const_iterator end = user.end();
			if(user.compare("") != 0)
			{
				if(boost::regex_search( start, end, results, gp))
				{
					// GX matched -- attempt insert into map<> with current timestamp, if already exists it will skip
					std::pair<std::map<std::string, time_t>::iterator, bool> ret;
					ret = GUEST_EXPIRATION.insert(std::pair<std::string, time_t>(user, time(NULL)));

					if(ret.second == false)
					{
						time_t t = GUEST_EXPIRATION[user];
						// Current account already exists, so check to see if expired
						time_t current = time(NULL);
						int msg_fifteen = ((2 * (60 * 60))-900);
						int msg_five = ((2 * (60 * 60))-300);
						int	 msg_one = ((2 * (60 * 60))-60);
						int msg_expired = ((2 * (60 * 60)));

						if(!fifteen && ( ((t + msg_five) >= current) && (current >= (t + msg_fifteen)) ) )
						{
							fifteen = true;

							HANDLE pipe = CreateNamedPipe(
								"\\\\.\\pipe\\lm_pipe", // name of pipe
								PIPE_ACCESS_OUTBOUND, // one way pipe -- outbound only
								PIPE_TYPE_BYTE, // send data as byte-stream
								1, // only allow 1 instance of this pipe
								0, // no outbound buffer
								0, // no inbound buffer
								0, // use default wait time
								NULL // use default security attributes
								);

							if(pipe == NULL | pipe == INVALID_HANDLE_VALUE){
								//std::cout << "FAILED TO CREATE PIPE!" << std::endl;
							}

							BOOL result = ConnectNamedPipe(pipe, NULL); // Blocks until client connects to the pipe
							if(!result){
								CloseHandle(pipe); // close the pipe
							}else{
								struct tm *_t;
								time_t long_t = time(NULL) + (60*15);
								_t = localtime(&long_t);
								char _buf[16];

								sprintf(_buf,"%d:%d",_t->tm_hour-12,_t->tm_min);
								string _msg = "Your Guest Account will expire in 15 minutes(";
								_msg.append(_buf);
								_msg.append(").  You will be logged out at that time\r\n");
								std::wstring _m = std::wstring(_msg.begin(), _msg.end());
								wchar_t *data = (wchar_t*)_m.c_str();
								DWORD numBytesWritten = 0;
								result = WriteFile(
									pipe,
									data,
									wcslen(data) * sizeof(wchar_t),
									&numBytesWritten,
									NULL
									);

								if(result){
									//std::cout << "Num Bytes written: " << numBytesWritten << std::endl;
								}else{
									//std::cout << "Failed to send data" << std::endl;
								}

								CloseHandle(pipe);
								//fifteen = true;
							}
						}
						else if( (fifteen && !five) && ( ((t + msg_one) >= current) && (current >= (t + msg_five)) ) )
						{
							five = true;

							HANDLE pipe = CreateNamedPipe(
								"\\\\.\\pipe\\lm_pipe", // name of pipe
								PIPE_ACCESS_OUTBOUND, // one way pipe -- outbound only
								PIPE_TYPE_BYTE, // send data as byte-stream
								1, // only allow 1 instance of this pipe
								0, // no outbound buffer
								0, // no inbound buffer
								0, // use default wait time
								NULL // use default security attributes
								);

							if(pipe == NULL | pipe == INVALID_HANDLE_VALUE){
								//std::cout << "FAILED TO CREATE PIPE!" << std::endl;
							}

							BOOL result = ConnectNamedPipe(pipe, NULL); // Blocks until client connects to the pipe
							if(!result){
								CloseHandle(pipe); // close the pipe
							}else{
								//const wchar_t *data = L"Your Guest Account will expire in 5 minutes.  You will be logged out at that time\r\n";
								struct tm *_t;
								time_t long_t = time(NULL) + (60*5);
								_t = localtime(&long_t);
								char _buf[16];

								sprintf(_buf,"%d:%d",_t->tm_hour-12,_t->tm_min);
								string _msg = "Your Guest Account will expire in 5 minutes(";
								_msg.append(_buf);
								_msg.append(").  You will be logged out at that time\r\n");
								std::wstring _m = std::wstring(_msg.begin(), _msg.end());
								wchar_t *data = (wchar_t*)_m.c_str();
								DWORD numBytesWritten = 0;
								result = WriteFile(
									pipe,
									data,
									wcslen(data) * sizeof(wchar_t),
									&numBytesWritten,
									NULL
									);

								if(result){
									//std::cout << "Num Bytes written: " << numBytesWritten << std::endl;
								}else{
									//std::cout << "Failed to send data" << std::endl;
								}

								CloseHandle(pipe);
								//five = true;
							}
						}
						else if( (fifteen && five && !one) && ( ((t + msg_expired) >= current) && (current >= (t + msg_one)) ) )
						{
							one = true;

							HANDLE pipe = CreateNamedPipe(
								"\\\\.\\pipe\\lm_pipe", // name of pipe
								PIPE_ACCESS_OUTBOUND, // one way pipe -- outbound only
								PIPE_TYPE_BYTE, // send data as byte-stream
								1, // only allow 1 instance of this pipe
								0, // no outbound buffer
								0, // no inbound buffer
								0, // use default wait time
								NULL // use default security attributes
								);

							if(pipe == NULL | pipe == INVALID_HANDLE_VALUE){
								//std::cout << "FAILED TO CREATE PIPE!" << std::endl;
							}

							BOOL result = ConnectNamedPipe(pipe, NULL); // Blocks until client connects to the pipe
							if(!result){
								CloseHandle(pipe); // close the pipe
							}else{
								//const wchar_t *data = L"Your Guest Account will expire in 1 minute.  You will be logged out at that time\r\n";
								struct tm *_t;
								time_t long_t = time(NULL) + (60*1);
								_t = localtime(&long_t);
								char _buf[16];

								sprintf(_buf,"%d:%d",_t->tm_hour-12,_t->tm_min);
								string _msg = "Your Guest Account will expire in 1 minute(";
								_msg.append(_buf);
								_msg.append(").  You will be logged out at that time\r\n");
								std::wstring _m = std::wstring(_msg.begin(), _msg.end());
								wchar_t *data = (wchar_t*)_m.c_str();
								DWORD numBytesWritten = 0;
								result = WriteFile(
									pipe,
									data,
									wcslen(data) * sizeof(wchar_t),
									&numBytesWritten,
									NULL
									);

								if(result){
									//std::cout << "Num Bytes written: " << numBytesWritten << std::endl;
								}else{
									//std::cout << "Failed to send data" << std::endl;
								}

								CloseHandle(pipe);
								//one = true;
							}
						}
						else if( (fifteen && five && one && !expired) && ( current >= (t + msg_expired) ) )
						{
							expired = true;

							HANDLE pipe = CreateNamedPipe(
								"\\\\.\\pipe\\lm_pipe", // name of pipe
								PIPE_ACCESS_OUTBOUND, // one way pipe -- outbound only
								PIPE_TYPE_BYTE, // send data as byte-stream
								1, // only allow 1 instance of this pipe
								0, // no outbound buffer
								0, // no inbound buffer
								0, // use default wait time
								NULL // use default security attributes
								);

							if(pipe == NULL | pipe == INVALID_HANDLE_VALUE){
								//std::cout << "FAILED TO CREATE PIPE!" << std::endl;
							}

							BOOL result = ConnectNamedPipe(pipe, NULL); // Blocks until client connects to the pipe
							if(!result){
								CloseHandle(pipe); // close the pipe
							}else{
								const wchar_t *data = L"Your Guest Account has expired!  You will be logged out now\r\n";
								DWORD numBytesWritten = 0;
								result = WriteFile(
									pipe,
									data,
									wcslen(data) * sizeof(wchar_t),
									&numBytesWritten,
									NULL
									);

								if(result){
									//std::cout << "Num Bytes written: " << numBytesWritten << std::endl;
								}else{
									//std::cout << "Failed to send data" << std::endl;
								}

								CloseHandle(pipe);
								//expired = true;
							}

							/// Wait 60 seconds and then kill all user applications and log them off
							/*

							*/
							kick_expired_accounts();

							fifteen = false;
							five = false;
							one = false;
							expired = false;
						}
					}
				}
			}
		}
		else
		{
			fifteen = false;
			five = false;
			one = false;
			expired = false;
			mSleep(1);
		}
		mSleep(60); // sleep 1 second
	}
#endif // _WIN32
}

void execute_script()
{
	struct tm *tm_struct;
	time_t t = time(NULL);
	tm_struct = localtime(&t);

	std::map<std::string, std::vector<std::string> >::iterator it = SCRIPTS.begin();
	for(it = SCRIPTS.begin(); it != SCRIPTS.end(); ++it)
	{
		int _hour, _min;
		std::string _s = it->second.at(tm_struct->tm_wday);
		std::vector<std::string> _v = parse_script2(_s, ":");
		_hour = atoi(_v.at(0).c_str());
		_min = atoi(_v.at(1).c_str());

		if(_hour == tm_struct->tm_hour && _min == tm_struct->tm_min)
		{
			// Execute script
			system(it->first.c_str());
		}
	}
}

std::vector<std::string> parse_script2(std::string script, std::string delim)
{
	std::vector<std::string> elements;
	boost::algorithm::split(elements, script, boost::algorithm::is_any_of(delim));
	return elements;
}

/** TODO **/
int get_time_limit(std::string account);
void linux_listen_thread(); /** This will be the listening point **/


/**
    Signal Handler -- Put any user defined signals here if we need to signal threads
**/
void sig_handler(int signo)
{
    /* Example  -- can't actually catch SIGKILL or SIGSTOP, but you can catch SIGINT */
    //if(signo == SIGKILL)
    //    printf("Can't catch me\n");

    if(signo == SIGINT)
        L_RUNNING = false;
}

void create_directories()
{
#ifdef __linux__
    int status = mkdir("/opt/monitoring", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	int dstatus = mkdir("/opt/monitoring/data", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	int cstatus = mkdir("/opt/monitoring/config", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

	if((status || dstatus || cstatus) != 0)
	{
        time_t tt = time(NULL);
        struct tm tm;
        char buf[32];
        tm = *localtime(&tt);
        strftime(buf, 31, "%Y-%m-%d %H:%M:%S", &tm);
        std::ofstream fLog (ERR_LOG, std::ios::app);
        if(fLog.is_open())
        {
            fLog << "ERROR in create_directories() -- " << strerror(errno) << " -- " << buf << "\n";
        }
        fLog.close();
	}
#endif

#ifdef _WIN32
	CreateDirectory("C:\\Tools\\Monitoring\\data", NULL);
	CreateDirectory("C:\\Tools\\Monitoring\\log", NULL);
	CreateDirectory("C:\\Tools\\Monitoring\\Config", NULL);
#endif
}

void mSleep(int ms)
{
#ifdef __linux__
	usleep(ms * 1000);
#endif
#ifdef _WIN32
	Sleep(ms * 1000);
#endif
}

void gather_data()
{
    /** TEMPORARY **/
    timeLimitAccounts.insert(std::pair<std::string,int>("gp", 2)); /** FIXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX **/

	while(true)
	{
		/* Run any scripts that need it */
		execute_script();

		/* Get logged in status */
		if(logged_in())
		{
std::cout << "LOGGED IN" << std::endl;
			check_allowed_accounts(BLOCKED_REGEX);

			LOGGED_IN = true;

			//kick_expired_accounts();

			int pcount = prog_number();
			//if(pcount == 0)
            //{
            //    set_program_list();
            //    pcount = prog_number();
            //}
std::cout << "pcount " << EVENTSIZE << std::endl;
			int block = 0;
			if(pcount % EVENTSIZE != 0)
				block = (pcount / EVENTSIZE) + 1;
			else
				block = (pcount / EVENTSIZE);

std::cout << "pcount - " << prog_number() << std::endl;
std::cout << "block - " << block << std::endl;
			//unsigned int blocks[block];
			unsigned int *blocks = new unsigned int[block]; /* Windows mod */
			unsigned int *r_blocks = new unsigned int[block]; /* Windows mod */
			for(int i = 0; i < block; ++i)
			{
                blocks[i] = 0;
                r_blocks[i] = 0;
            }


			//unsigned int *running_tally = new unsigned int[block];//unsigned int *running_blocks = new unsigned int[block]; /* Running tally of programs, added onto every 5 seconds */
			std::string pdata,rpdata;

			// While UNIX epoch timestamp is less than current minute + 60 seconds loop and every 5 seconds within that window poll the running programs and logically OR with a running
			// tally of programs found running.  Once the window has elapsed we can write to disk
			struct tm *tm_struct, *tm_struct1;
			time_t t = time(NULL);
			tm_struct = tm_struct1 = localtime(&t);
//std::cout << "Gathering for minute: " << tm_struct->tm_min << std::endl;
			while(time(NULL) < (t + 59))
			{
				time_t nt = time(NULL);
				tm_struct1 = localtime(&nt);
				for(int i = 0; i < block; i++)
				{
					std::string processes = linux_get_running_proc(get_current_local_user()); /** TODO!!!!  -- need to deal with remote logins on linux machines (windows?) **/
					unsigned int tally = tally_program_count(processes, i+1);
					blocks[i] = blocks[i] | tally;
					CURRENT_EVENT_BLOCK = blocks;
				}
				for(int i = 0; i < block; i++)
				{
					std::string r_processes = linux_get_running_proc(get_current_remote_user()); /** TODO!!!!  -- need to deal with remote logins on linux machines (windows?) **/
					unsigned int r_tally = tally_program_count(r_processes, i+1);
					r_blocks[i] = r_blocks[i] | r_tally;
					//CURRENT_EVENT_BLOCK = r_blocks; // need to implement this later in LISTEN i think...
				}


				if(tm_struct1->tm_sec < 55)
					mSleep(5);
				else
					mSleep(60 - tm_struct1->tm_sec);
			}
			for(int i = 0; i < block; i++)
			{
				char buffer[32];
				char rbuffer[32];
				sprintf(buffer, "%08X", blocks[i]);
				sprintf(rbuffer, "%08X",r_blocks[i]);
				rpdata += rbuffer;
				pdata += buffer;
			}

            std::vector<unsigned char> EVENT,REVENT;
            if(get_current_local_user().length()>0)
                EVENT = build_event(pdata, get_current_local_user());
            if(get_current_remote_user().length()>0)
                REVENT = build_event(rpdata, get_current_remote_user());

			/* NEW windows mod*/
			delete blocks;

			/* Store the EVENT in case the Client is unable to send it to the Server */
			try{
				std::string line = "", rline = "";
				for(unsigned i = 0; i < EVENT.size(); i++)
				{
					char c = EVENT[i];
					line = line + c;
				}

				/* Add the current EVENT to the back of the vector */
				EVENTS.push_back(line);

				for(unsigned i = 0; i < REVENT.size(); i++)
				{
					char c = REVENT[i];
					rline = rline + c;
				}
				EVENTS.push_back(rline);

/*** BELOW is logging to the EVENTS file...need to TEST THIS!!! ***/
				line = line + "\n";
				ofstream efile;
				try{
				efile.open(EVENT_FILE, std::ios_base::app);
				if(efile.is_open())
				{
					efile << line.c_str();

				}
				else
				{
					efile.open(EVENT_FILE, std::ios_base::app);
					efile << line.c_str();
				}
				}catch(std::exception e)
				{
					/** LOGGING TODO **/
				}
				efile.close();
			}catch(std::exception &e){
				/** LOGGING TODO **/
				std::cout << "Gather EVENT File exception: " << e.what() << std::endl;
			}
/*** END TESTING SECTION ***/
		}
		else
		{
			LOGGED_IN = false;
		}

		/* Cleanup resources */
		resource_cleanup();

		mSleep(1);
	}
}

void check_allowed_accounts(std::string br)
{
#ifdef _WIN32
	if(br.compare("") == 0)
		return;
	std::vector<std::string> _regexs;
	boost::split(_regexs, br, boost::is_any_of(","));
	std::string _u = currentUsers.at(0).name;
	std::string::const_iterator start = _u.begin();
	std::string::const_iterator end = _u.end();

	boost::match_results<std::string::const_iterator> results;

	for(int i = 0; i < _regexs.size(); ++i)
	{
		boost::regex _r(_regexs.at(i), boost::regex::perl|boost::regex::icase);

		if(boost::regex_search(start, end, results, _r))
		{
			HANDLE pipe = CreateNamedPipe(
								"\\\\.\\pipe\\lm_pipe", // name of pipe
								PIPE_ACCESS_OUTBOUND, // one way pipe -- outbound only
								PIPE_TYPE_BYTE, // send data as byte-stream
								1, // only allow 1 instance of this pipe
								0, // no outbound buffer
								0, // no inbound buffer
								0, // use default wait time
								NULL // use default security attributes
								);

							if(pipe == NULL | pipe == INVALID_HANDLE_VALUE){
								//std::cout << "FAILED TO CREATE PIPE!" << std::endl;
							}

							BOOL result = ConnectNamedPipe(pipe, NULL); // Blocks until client connects to the pipe
							if(!result){
								CloseHandle(pipe); // close the pipe
							}else{
								const wchar_t *data = L"Your Account is not allowed on this machine.\r\n";
								DWORD numBytesWritten = 0;
								result = WriteFile(
									pipe,
									data,
									wcslen(data) * sizeof(wchar_t),
									&numBytesWritten,
									NULL
									);

								if(result){
									//std::cout << "Num Bytes written: " << numBytesWritten << std::endl;
								}else{
									//std::cout << "Failed to send data" << std::endl;
								}

								CloseHandle(pipe);
							}
			mSleep(5000);
			kick_expired_accounts();
		}
	}
#endif
}

/** Testing async_accpet **/
void handle_accept(const boost::system::error_code& error)
{
	if(!error)
	{
		// Accepted!
		//std::cout << "ACCCEPTED A CONNECTION!!!" << std::endl;
	}
}

void linux_listen_thread()
{
	unsigned int totalSend;
    while(true)
    {
        //signal(SIGINT, sig_handler); // register SIGINT

		time_t tt = time(NULL);
        struct tm tm;
        char buf[32];
        tm = *localtime(&tt);
        std::cout << "Starting listen at minute: " << tm.tm_min << ":" << tm.tm_sec << std::endl;

        /* Set the running flag so the application knows this thread is active and we don't need to join it for testing */
        L_RUNNING = true;

        std::vector<char> event;

        try{
            boost::asio::io_service io_service;

            // Listen for incoming connections
			boost::asio::ip::tcp::acceptor acceptor(io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), LISTEN_PORT));
			acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
			//acceptor.listen();

            for(;;)
            {
                boost::asio::ip::tcp::socket socket(io_service);

#ifdef _WIN32
				int32_t timeout = 10000;
				setsockopt(socket.native(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
				setsockopt(socket.native(), SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#endif
#ifdef __linux__
				struct timeval tv;
				tv.tv_sec = 10;
				tv.tv_usec = 0;
				setsockopt(socket.native(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
				setsockopt(socket.native(), SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#endif
				acceptor.accept(socket); // Wait for connection from client

                // Client connected
                boost::system::error_code error;

                boost::array<unsigned char, 4096> tmp; // 4k (should be enough??)
				size_t len = socket.read_some(boost::asio::buffer(tmp), error);
				tmp[len] = '\0';

				if(tmp[0] == '2' && len == 1)
                {
                    /* Send EVENTs */
					if(LOGGED_IN)
					{
						time_t tt = time(NULL);
						struct tm tm;
						char buf[32];
						tm = *localtime(&tt);
						std::cout << "Got: " << tmp.data() << " at minute: " << tm.tm_min << std::endl;

						// Read all EVENTs in from file
						size_t len = 0;
						size_t read;

						std::string line;

						totalSend = 0;
						std::ofstream fLog (ERR_LOG, std::ios::app);
						if(fLog.is_open())
						{
							fLog << "Server sent: " << tmp.data() << " size of EVENTS: " << EVENTS.size() << "\n";
						}
						fLog.close();

						// Read in stored EVENTS then clear the file
						std::ifstream wfp(EVENT_FILE);
						std::string wline;
						while(wfp.good())
						{
							getline(wfp,wline);
							std::cout << "File line length: " << wline.length() << std::endl;
							if(wline.length() > 0)
							{
                                EVENTS.push_back(wline);
                            }
						}
						wfp.close();
						std::fstream _f(EVENT_FILE, ios::in);
						if(_f)
						{
							_f.close();
							_f.open(EVENT_FILE, ios::out | ios::trunc);
						}
						_f.close();

						/** Remove any exact duplicate EVENTS, although it is taken care of at the MySQL level, this will prevent that overhead for a little overhead here **/
						std::sort(EVENTS.begin(), EVENTS.end());
						EVENTS.erase(std::unique(EVENTS.begin(), EVENTS.end()), EVENTS.end());

						/** TESTING current EVENT **/
						if(EVENTS.empty())
						{
                            line = get_current_event();
                            const char* end = line.c_str() + strlen(line.c_str());
                            event.insert(event.end(), line.c_str(), end);
                            size_t sent = boost::asio::write(socket, boost::asio::buffer(event), boost::asio::transfer_all(), error);
                            event.clear();
                            totalSend += sent;
						}

						while(!EVENTS.empty())
						{
							line = EVENTS.back();
							std::cout << "LINE: " << line << " " << time(NULL) << std::endl;
                            EVENTS.pop_back();
                            const char* end = line.c_str() + strlen(line.c_str());
                            event.insert(event.end(), line.c_str(), end);
                            size_t sent = boost::asio::write(socket, boost::asio::buffer(event), boost::asio::transfer_all(), error);
                            event.clear();
                            totalSend += sent;

                            std::ofstream fLog (ERR_LOG, std::ios::app);
                            if(fLog.is_open())
                            {
                                fLog << "Server sent: " << tmp.data() << " -- response: " << line << "\n";
                            }
                            fLog.close();
						}
						// Clear file
						//fstream f(EVENT_FILE, ios::out | ios::trunc);
						LAST_SERVER_COMMUNICATION = time(NULL);
					}
					else
					{
						char c[1];
						c[0] = '8';
						c[1] = '\0';
						size_t sent = boost::asio::write(socket, boost::asio::buffer(c), boost::asio::transfer_all(), error);
					}
                }
                if(tmp[0] == '1')
                {
                    /* Read in new Program List file */
					std::string s = (char *)tmp.data();
					s = s.substr(1);
                    std::remove(P_FILE);
                    std::ofstream ofile;
                    ofile.open(P_FILE);

                    vector<string> fields;
                    boost::algorithm::split(fields, s, boost::algorithm::is_any_of(":"));
                    for(size_t n = 0; n < fields.size(); n++)
                    {
                        if(fields[n].compare("") != 0)
                        {
                            ofile << fields[n]; /* write to file */
                            ofile << "\n";
                        }
                    }

                    ofile.close(); /* close file */

					/* Needed??? *///LAST_SERVER_COMMUNICATION = time(NULL);
                }

                /** TEST **/
                //boost::asio::write(socket, boost::asio::buffer(event), boost::asio::transfer_all(), error);
                /** **/

                // read_some() will exit with boost::asio::error::eof which is how we know to break the loop
                if(error == boost::asio::error::eof){
                    //break;
                }
                else
                {
                    //std::cout << "ERROR43: " << strerror(errno) << std::endl;
#ifdef _WIN32
                    throw boost::system::system_error(error); // Throw some other error
#endif
                }

                /* Successfully called home, so reset last communication with Server */
				LAST_SERVER_COMMUNICATION = time(NULL);

				socket.shutdown(boost::asio::socket_base::shutdown_both); // testing
				socket.close();
				acceptor.close();

				// test
				mSleep(30000);
            }
        }catch(std::exception &e){
            time_t tt = time(NULL);
            struct tm tm;
            char buf[32];
            tm = *localtime(&tt);
            strftime(buf, 31, "%Y-%m-%d %H:%M:%S", &tm);
            std::ofstream fLog (ERR_LOG, std::ios::app);
            if(fLog.is_open())
            {
                fLog << e.what() << " -- " << buf << "\n";
            }
            fLog.close();
        }

		std::cout << "AFTER SEND CYCLE: " << EVENTS.size() << std::endl;
    }
}

/**
    Takes in the running program string, and the currently locally logged in username and builds an EVENT frame into a char array.  It packs the frame type flag
    as an int == 2 signifying that this is an EVENT into the first byte, then it packs
    the program data in HEX format by blocks of 8 chars (or 32 bits).  The number of blocks depends on how many programs are currently being monitored,
    with 1 bit per program.  At a minimum there will be a single block up to (n) blocks.  The bit are associated to each program in the monitored list, and
    order matters so that the first program in the list is the first bit position in the first block.  If the program is found running then that bit is turned on.
    The block bits are then converted to HEX format and packed into the EVENT char array.

    Following the program data the next 2 bytes hold the length of the user name.  This allows for varrying usernames.  After those 2 bytes are (m) bytes holding the
    currently logged in locally username as chars.

    Following that is the current UNIX epoch timestamp rounded down to the nearest minute (by subtracting current tm_sec from current timestamp).  This is converted to chars
    and packed in.  There is no need for any bytes to represent the length of the timestamp since it will always be 10, and its start position can be determined from the length
    of the user name.

    Byte 0              - Flag
    Byte 1-2            - Length of Pdata
    Byte 3-n            - Pdata
    Byte (n+1)          - Length of User name
    Byte (n+2) - m      - User name
    Byte (m+1) - (m+11) - Timestamp

    @param string pdata - String of program data on a linux machine which is a straight dump of the ps -ax system command
    @param string user  - String representing the user name that is locally logged in
    @ret   char*        - Char array that is the packed EVENT frame
**/
std::vector<unsigned char> build_event(std::string pdata, std::string user)
{
    // Build an event and return
    /* 1 bytes == length of pdata
       pdata.length()
       2 bytes == length of user name
       10 bytes == timestamp
       1 byte == '\0'
    */
	std::cout << "BUILDING EVENT FOR -- " << user << std::endl;
    std::vector<unsigned char> EVENT;
    char buffer[16];

    /* Pack the frame type flag */
    EVENT.push_back((int)(((char)'0')+2));

    /* Pack the number of BLOCKS into the EVENT buffer */
    sprintf(buffer, "%d", ((int)pdata.length())/8);
    EVENT.push_back(buffer[0]);

   /* Pack the program data into the EVENT buffer, casting to chars, in Hex format */
    int i = 0;
    for(i; i < pdata.length(); i++)
    {
        EVENT.push_back(pdata[i]);
    }

    /* Pack the length of the user name into the EVENT buffer */
    sprintf(buffer, "%X", (unsigned int)user.length());
    if(user.length() < 16)
	{
        EVENT.push_back('0'); // Take up the first 4 bits of the User Name length space if length is less than 16, otherwise buffer[0] should fill it
		EVENT.push_back(buffer[0]);
	}
	else
	{
		EVENT.push_back(buffer[0]);
		EVENT.push_back(buffer[1]);
	}
    //EVENT.push_back(buffer[0]);

    for(i = 0; i < user.length(); i++)
    {
        EVENT.push_back(user[i]);
    }

    /* Add in the rounded timestamp to the EVENT frame */
    std::string tstamp = get_rounded_timestamp();
    for(i = 0; i < 10; i++)
    {
        EVENT.push_back((unsigned char)tstamp[i]);
    }

    return EVENT;
}

/**
    Gets the current UNIX epoch timestamp and subtracts the current tm_sec from it and returns it as a char*
    @ret char* - Current UNIX epoch timestamp minus the current seconds
**/
std::string get_rounded_timestamp()
{
    // Get UNIX epoch timestamp rounded DOWN to the nearest minute
    time_t ctime = get_timestamp();
    struct tm *loctime = localtime(&ctime);
    int seconds = loctime->tm_sec;
    char buf[128];
    sprintf(buf, "%d", (int)ctime-seconds);
    //std::cout << "buf: " << buf << std::endl;
    std::string ret = buf;
    return ret;
}

/**
    Gets the current locally logged in user account name.
    @ret string - User name
**/
std::string get_current_local_user()
{
	for(std::vector<user>::iterator it = currentUsers.begin(); it != currentUsers.end(); ++it)
	{
		if((*it).local)
		{
			std::string temp = (*it).name;
			return temp;
		}
	}
	return "";
}

/**
    Gets the current remotely logged in user account name.
    @ret string - User name
**/
std::string get_current_remote_user()
{
	for(std::vector<user>::iterator it = currentUsers.begin(); it != currentUsers.end(); ++it)
	{
		if(!(*it).local)
		{
			std::string temp = (*it).name;
			return temp;
		}
	}
	return "";
}

/**
    Gets the current number of programs being monitored from the master list file.
    @ret int - Number of programs in the file
**/
int prog_number()
{
	return PROGRAM_COUNT;
}

int win_prog_number()
{
	int n = 0;
	std::ifstream wfp("C:\\Tools\\Monitoring\\Config\\masterlist.txt");
	std::string wline;
	while(wfp.good())
	{
		getline(wfp,wline);
		n++;
	}
	wfp.close();
	n--;
	//std::cout << "Windows prog_number == " << n << std::endl;
	if(n == 0)
		REQUEST_PFILE = true;
	else
		REQUEST_PFILE = false;
    return n;
}

/* TODO -- test and QA this function on Linux */
int linux_prog_number()
{
    int n = 0;
    char *line = NULL;
    size_t len = 0;
    FILE *fp;
    size_t read;

    fp = fopen(P_FILE, "r");
    if(fp == NULL)
    {
        /** Log File couldn't open program list **/
        time_t tt = time(NULL);
        struct tm tm;
        char buf[32];
        tm = *localtime(&tt);
        strftime(buf, 31, "%Y-%m-%d %H:%M:%S", &tm);
        //std::cout << buf << std::endl;
        std::ofstream fLog (ERR_LOG, std::ios::app);
        if(fLog.is_open())
        {
            fLog << "linux_prog_number(): Failed to open program list -- " << buf << "\n";
        }
        fLog.close();
std::cout << "prog file pointer NULL" << std::endl;
        return n;
    }
#ifdef __linux__
std::cout << "HEREasdf" << std::endl;
    while((read = getline(&line, &len, fp)) != -1)
	{
        if(read > 1)
        {
            // increment program counter
            std::cout << "Read " << line << std::endl;
            n++;
        }
    }
#endif
	fclose(fp);
    return n;
}

void linux_set_program_list()
{
#ifdef __linux__
	// Read masterlist
    char *line = NULL;
    size_t len = 0;
    FILE *fp;
    size_t read;

	pf_mutex.lock();
    fp = fopen(P_FILE, "r");

    if(fp == NULL)
    {
        /** Log File couldn't open program list **/
        time_t tt = time(NULL);
        struct tm tm;
        char buf[32];
        tm = *localtime(&tt);
        strftime(buf, 31, "%Y-%m-%d %H:%M:%S", &tm);
        //std::cout << buf << std::endl;
        std::ofstream fLog (ERR_LOG, std::ios::app);
        if(fLog.is_open())
        {
            fLog << "linux_tally_program_count(): Failed to open program list -- " << buf << "\n";
        }
        fLog.close();
        //return current_progs;
    }
    else
    {
        int count = 0;
        while((read = getline(&line, &len, fp)) != -1)
        {
            if(read > 1)
            {
                PROGRAM_LIST.push_back(line);
                count++;
            }
            if(read == 1)
            {
                PROGRAM_LIST.push_back("");
                count++;
            }
        }
        free(line);
        fclose(fp);
        pf_mutex.unlock();
        PROGRAM_COUNT = count;
	}
#endif
}

void win_set_program_list()
{
	pf_mutex.lock();
	ifstream FILE("C:\\Tools\\Monitoring\\Config\\masterlist.txt");
	string line;
	if(FILE.is_open())
	{
		int count = 0;
		while(FILE.good())
		{
			getline(FILE, line);
			PROGRAM_LIST.push_back(line);
			count++;
		}
		FILE.close();
		PROGRAM_COUNT = count;
	}
	pf_mutex.unlock();
}

void set_program_list()
{
#ifdef _WIN32
	win_set_program_list();
#endif
#ifdef __linux__
	linux_set_program_list();
#endif
}

bool win_find_running_process(std::string process)
{
#ifdef _WIN32
	char* compare;
	bool isRunning = false;

	HANDLE hProcessSnap;
	PROCESSENTRY32 pe32;
	hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);


	process = process + ".exe";

	//std::cout << "Looking for: " << process << std::endl;
	if(hProcessSnap == INVALID_HANDLE_VALUE)
	{
		isRunning = false;
	}
	else
	{
		pe32.dwSize = sizeof(PROCESSENTRY32);
		// Get first running process
		if(Process32First(hProcessSnap, &pe32))
		{
			if(pe32.szExeFile == process)
			{
				//std::cout << "Found: " << process << std::endl;
				isRunning = true;
			}
			else
			{
				// Loop through all running processes looking for the given process
				while(Process32Next(hProcessSnap, &pe32))
				{
					compare = pe32.szExeFile;
					std::string s = std::string(compare);
					//if(s.compare(process) == 0)
					if(boost::iequals(s, process))
					{
						// if found running, set to true and break loop
						//std::cout << "Found: " << process << std::endl;
						isRunning = true;
						break;
					}
				}
			}
			CloseHandle(hProcessSnap);
		}
	}

	return isRunning;
#endif
}

/**
    For a given block and the current program data this function returns an integer with bits flipped that
    represents which programs are currently running and within this block.
    @param string - List of processes running on the machine
    @param int    - Block that the programs are in
    @ret   unsigned int - Represents which programs in this block are running
**/
unsigned int tally_program_count(std::string ptree, int block)
{
    //std::cout << "COUNT: " << PROGRAM_LIST.size() << std::endl;
    for(int j = 0; j < PROGRAM_LIST.size(); j++)
    {
        //std::cout << PROGRAM_LIST[j] << " " << (j+1) << std::endl;
    }
	unsigned int current_progs = 0;
	int bit_position = 0, cline = (((block-1)*32)+1);
	for(int index = (block-1) * 32; index < block*32; index++)
	{
		if(index >= PROGRAM_LIST.size())
			return current_progs;

		if( ( cline >= ((block-1)*32) ) && ( cline <= (block*32)  && ( PROGRAM_LIST[index].compare("") != 0 ) ) )
		{
			// Compare against what we found running in processes
			size_t found;

#ifdef __linux__
			found = ptree.find(PROGRAM_LIST[index]);
			if(found != std::string::npos)
			{
				// Set the bit position to 1
				//std::cout << "Bit position: " << bit_position << " found: " << PROGRAM_LIST[index] << " index: " << index << " in block: " << block << " at position: " << found << std::endl;
				//current_progs = (current_progs | (1 << bit_position) ); /**************** false positives ******************/
				current_progs = (current_progs | (1 << index) );
			}
#endif
#ifdef _WIN32
			if(win_find_running_process(PROGRAM_LIST[index]))
			{
				current_progs = (current_progs | (1 << bit_position) );
			}
#endif
			// Increment bit position
			bit_position++;
		}
		cline++; // Increment file line count
	}
	return current_progs;
}

/**
    For the given user name on a linux system collect all running processes and compare that against the list of watched programs to determine which, if any
    are running on the system

    @param current_user String user name that is running proccesses
    @ret result String output of system command ps -u
**/
std::string linux_get_running_proc(std::string current_user)
{
    std::string cmd = "ps -u " + current_user;
    std::string result = "";
    char *command = new char[cmd.length()+1];
    strcpy(command, cmd.c_str());

    FILE *fp;
#ifdef __linux__
    fp = popen(command, "r");
#endif
#ifdef _WIN32
	fp = _popen("tasklist", "r");
#endif
    if(fp == NULL)
    {
        return "";
    }
    while(fgets(command, sizeof(command), fp) != NULL)
    {
        result += command;
    }
#ifdef __linux__
    pclose(fp);
#endif

    delete [] command;

    return result;
}

/** Cleanup any resources used in preparation for the next iteration
**/
void resource_cleanup()
{
	//cu_mutex.lock();
    currentUsers.clear();
	//cu_mutex.unlock();
    loginRestrictedAccounts.clear();
    timeLimitAccounts.clear();
}


void kick_expired_accounts()
{
#ifdef _WIN32
	try{
		SetLastError(0);
		if(!WTSLogoffSession(WTS_CURRENT_SERVER_HANDLE, WTSGetActiveConsoleSessionId(),FALSE))
		{
			char *msg = "kick_expired_accounts() error\n";
			Log(msg);
		}
	}catch(std::exception &e)
	{
	}
#endif
}

/**
    Look up the account in the time limit map and return the time limit for that account type.  If multiple matches are found to match the given current user
    account on the time restricted accounts list then the first match found is returned.  So the config file for time restricted accounts relies on order of precedence.
    @param  account a string user account in the current users container
    @ret            an integer time limit associated with the time restricted account found to match the account parameter, -1 returned on no match found
**/
int get_time_limit(std::string account)
{
    std::map<std::string, int>::iterator it;
    for(it = timeLimitAccounts.begin(); it != timeLimitAccounts.end(); it++)
    {
        // if KEY == account return TRUE
        boost::regex needle("\\b(" + (*it).first + ")");
        const std::string haystack = account;
        boost::match_results<std::string::const_iterator> what;

        if(boost::regex_search(haystack.begin(), haystack.end(), what, needle, boost::match_default))
        {
            return (*it).second;
        }
    }

    /* No match found return -1 */
    return -1;
}


/**
    Get the current timestamp
    @ret    timestamp time_t is returned for the current system time
**/
time_t get_timestamp()
{
    return time(NULL);
}

/**
    Adds the time limit for the given restricted account to that accounts started time, and compares it against the current system time.  If it is greater then it will
    return true, otherwise the account hasn't expired yet and returns false.
    @param  started The time_t started timestamp for the current user
    @param  limit   The time limit associated with the restricted account in hours
    @ret    bool    Whether or not the account has passed expiration
**/
bool time_expired(time_t started, int limit)
{
    /* Get the current system time */
    time_t current_time = time(NULL);

    /* Add the time limit in hours to the started time for the current user */
    time_t users_time = started + (limit * 60 * 60);

    if(users_time >= current_time) /** CHANGE BACK TO < after testing **/
        return true;
    return false;
}

/**
    Kill All user processes on a Linux machine and log them off
    @param string u User account to be logged off
**/
void linux_logoff_user(std::string u)
{
    FILE *f;
    std::string cmd = "killall -u " + u;
#ifdef __linux__
    f = popen(cmd.c_str(), "w");
	pclose(f);
#endif
#ifdef _WIN32
	f = _popen(cmd.c_str(), "w");
	fclose(f);
#endif
}

/**
    Sends a message to a locally logged in account that is restrictred and should not be logged into this machine, the message is defined in the config file.
    This uses Xmessage on linux and so it must be threaded in order to prevent the dialog box blocking
**/
void send_local_restricted_message()
{
/** TODO!!!!!!!!!!!!! **/
/*
    FILE *f;
    std::string cmd = "xmessage -center ";
    cmd += LOCAL_RESTRICTED_MSG;
    cmd += " -buttons ok";

#ifdef __linux__
	f = popen(cmd.c_str(), "w");
	pclose(f);
#endif
#ifdef _WIN32
	f = _popen(cmd.c_str(), "w");
	fclose(f);
#endif
*/
}

/**
    Sends a message to a locally logged in account that is time restricted and has passed its expiration time limit.  The message is defined in the configuration of the server
**/
void send_local_time_message()
{
/** TODO!!!!!!!!!!!!!!!!!!!! **/
/*
    FILE *f;
    std::string cmd = "xmessage -center ";
    cmd += LOCAL_TIME_MSG;
    cmd += " -buttons ok";

#ifdef __linux__
	f = popen(cmd.c_str(), "w");
	pclose(f);
#endif
#ifdef _WIN32
	f = _popen(cmd.c_str(), "w");
	fclose(f);
#endif
*/
}

/**
    Sends a message to a remotely logged in account that is restricted and should not be logged into this machine, the message is #defined so you can adjust it above.
    This uses echo and pipes it out to the users PTS in /dev/pts/#, and sends an EOF
**/
void send_remote_restricted_message(std::string pts)
{
/** TODO!!!!!!!!!!! **/
/*
    FILE *f;
    size_t pos = pts.find("/");

    if(pos != std::string::npos)
    {
        std::string s = pts.substr(pts.find("/")+1);
        std::string cmd = "echo ";
        cmd += REMOTE_RESTRICTED_MSG;
        cmd += " > /dev/pts/";
        cmd += s;
        cmd += " << EOF";

#ifdef __linux__
        f = popen(cmd.c_str(), "w");
		pclose(f);
#endif
#ifdef _WIN32
		f = _popen(cmd.c_str(), "w");
		fclose(f);
#endif

    }
    else
    {
        time_t tt = time(NULL);
        struct tm tm;
        char buf[32];
        tm = *localtime(&tt);
        strftime(buf, 31, "%Y-%m-%d %H:%M:%S", &tm);
        std::ofstream fLog (ERR_LOG, std::ios::app);
        if(fLog.is_open())
        {
            fLog << "send_remote_restricted_message(): Failed to send message -- " << buf << "\n";
        }
        fLog.close();
    }
    */
}

/**
    Sends a message to a time restricted remotely logged in account that their time limit has expired
**/
void send_remote_time_message(std::string pts)
{
/** TODO!!!!!!!!!!!!!!!!!!!! **/
/*
    FILE *f;
    size_t pos = pts.find("/");
    if(pos != std::string::npos)
    {
        std::string s = pts.substr(pts.find("/")+1);
        std::string cmd = "echo ";
        cmd += REMOTE_TIME_MSG;
        cmd += " > /dev/pts/";
        cmd += s;
        cmd += " << EOF";

#ifdef __linux__
		f = popen(cmd.c_str(), "w");
        pclose(f);
#endif
#ifdef _WIN32
		f = _popen(cmd.c_str(), "w");
		fclose(f);
#endif
    }
    else
    {
        time_t tt = time(NULL);
        struct tm tm;
        char buf[32];
        tm = *localtime(&tt);
        strftime(buf, 31, "%Y-%m-%d %H:%M:%S", &tm);
        //std::cout << buf << std::endl;
        std::ofstream fLog (ERR_LOG, std::ios::app);
        if(fLog.is_open())
        {
            fLog << "send_remote_time_message(): Error sending message -- " << buf << "\n";
        }
        fLog.close();
    }
    */
}

/**
    Checks to see if a Linux machine is logged in.  If the configuration asks for local or remote logins it will record those.  There can only be a single local
    user, but many remote users logged in.

    @ret Boolean value determining if either a local / remote login is active if the client is configured to watch both, otherwise just checks for local logins
**/
bool logged_in()
{
#ifdef __linux__
	return linux_logged_in();
#endif
	return win_logged_in();
}

bool win_logged_in()
{
#ifdef _WIN32

	vector<string> olines;
	int localCount = 0;

	std::string username = "";
	char szTempBuf[MAX_PATH] = {0};
	  HANDLE hToken    = NULL;
	  HANDLE hDupToken = NULL;
	  // Get the user of the "active" TS session
	  DWORD dwSessionId = WTSGetActiveConsoleSessionId();
	  if ( 0xFFFFFFFF == dwSessionId )
	  {
		// there is no active session
	  }
	  WTSQueryUserToken(dwSessionId, &hToken);
	  if ( NULL == hToken )
	  {
		// function call failed
	  }
	  DuplicateToken(hToken, SecurityImpersonation, &hDupToken);
	  if ( NULL == hDupToken )
	  {
		CloseHandle(hToken);
	  }
	  BOOL bRes = ImpersonateLoggedOnUser(hDupToken);
	  if ( bRes )
	  {
		// Get the username
		DWORD dwBufSize = sizeof(szTempBuf);
		bRes = GetUserNameA(szTempBuf, &dwBufSize);
		RevertToSelf(); // stop impersonating the user
		if ( bRes )
		{
		  // the username string is in szTempBuf
		}
	  }
	  CloseHandle(hDupToken);
	  CloseHandle(hToken);
	/**  ************* **/

  /** REMOVING THIS WHILE DEBUGGIN **/
	username = szTempBuf;

/** Remove below code when finished debuggin **/
//char un[UNLEN+1];
//DWORD username_size = sizeof(un);
//GetUserName(un, &username_size);
//username = un;
/** When done debugging remove the above stuff ^^ **/
	if(username.size() > 0)
		olines.push_back(username);

	for(int i = 0; i < olines.size(); i++)
    {
        bool isRemote = false;
        bool isLocal = false;
        std::string shell;
        std::string tty;
        std::string name;

        isLocal = true;
        isRemote = false;
        shell = "win";
		user u;
		u.name = username;
		u.terminal = shell;
		u.started = get_timestamp();
        //currentUsers.push_back({.name = name, .terminal = shell, .local = false, .started = get_timestamp()});
		//cu_mutex.lock();
		currentUsers.push_back(u);
		//cu_mutex.unlock();
    }

#endif
	//cu_mutex.lock();
	if(currentUsers.size() > 0)
	{
		//cu_mutex.unlock();
        return true;
	}
    else
	{
		//cu_mutex.unlock();
        return false;
	}
}

bool linux_logged_in()
{
	char x[200];
    vector<string> olines;
	int localCount = 0;

	FILE *f;
#ifdef __linux__
    f = popen("who", "r");
#endif
	while(fgets(x, 200, f) != NULL)
    {
        olines.push_back(x);
    }

#ifdef __linux__
	pclose(f);
#endif

	// Sort the output alphabetically
	std::sort(olines.begin(), olines.end());
	olines.erase( std::unique( olines.begin(), olines.end() ), olines.end() );

    /*
        For each line returned by the 'who' linux command, split the line string by whitespace, and determine the user name, and login session
    */
    std::string name;
    for(int i = 0; i < olines.size(); i++)
    {
        vector<string> splits = split_string(olines[i], " ", true); // REMOTE boolean flag determines if we are checking for remote sessions??

        for(int i2 = 0; i2 < splits.size(); i2++)
        {
            if(name.compare(splits[0]) != 0)
            {
                // Last found user name is different from the current user name so we have found a new user
                name = splits[0]; // set user name

                // Determine if remote or local
                if(splits[1].compare(":0") == 0)
                {
                    // Local
                    user u;
					u.name = name;
					u.terminal = "pts/0";
					u.local = true;
					u.started = get_timestamp();
                    currentUsers.push_back(u);
                }
                else
                {
                    if(splits[5].compare("(:0)") != 0)
                    {
                        // Remote session
                        std::cout << "Adding remote: " << name << " - " << splits[5].substr(1,splits[5].length()-2) << std::endl;
                        user u;
                        u.name = name;
                        u.terminal = splits[5].substr(1,splits[5].length()-2);
                        u.local = false;
                        u.started = get_timestamp();
                        currentUsers.push_back(u);
                    }
                }
            }
        }
    }

    if(currentUsers.size() > 0)
	{
        return true;
	}
    else
	{
        return false;
	}
}

/**
    Split a string by the given delimiter and return a vector of the fields
    @param s        String to be split
    @param delim    String delimiter to split on
    @param compress Bool value whether to compress tokens, if multiple deliminaters are found next to each other it will compress them and treat them as a single delim
    @ret   fields   Vector<string> containing the split up string
**/
vector<std::string> split_string(std::string s, string delim, bool compress)
{
    vector<std::string> fields;

    /* Strip newline */
    s.erase(s.find_last_of("\n"));

    if(!compress)
    {
        boost::algorithm::split(fields, s, boost::algorithm::is_any_of(delim));
        return fields;
    }
    boost::algorithm::split(fields, s, boost::algorithm::is_any_of(delim), boost::algorithm::token_compress_on);

    return fields;
}

/**
    Write new Program File from Server
**/
void write_program_file(std::string list)
{
	std::vector<std::string> _splits;
	boost::split(_splits, list, boost::is_any_of(","));
	// If file exist delete it
#ifdef __linux__
	fstream _f(P_FILE);
	if(_f.good())
	{
		// Overwrite file
		pf_mutex.lock();
		//_f.open("C:\\Tools\\Monitoring\\Config\\masterlist.txt");
		for(int i = 0; i < _splits.size(); ++i)
		{
			//if(i < _splits.size()-1)
			if(i < _splits.size())
				_f << _splits[i] + "\n";
			else
				_f << _splits[i];
		}
		_f.close();
		pf_mutex.unlock();
	}
#endif
#ifdef _WIN32
	fstream _f("C:\\Tools\\Monitoring\\Config\\masterlist.txt");
	if(_f.good())
	{
		// Overwrite file
		pf_mutex.lock();
		//_f.open("C:\\Tools\\Monitoring\\Config\\masterlist.txt");
		for(int i = 0; i < _splits.size(); ++i)
		{
			if(i < _splits.size()-1)
				_f << _splits[i] + "\n";
			else
				_f << _splits[i];
		}
		_f.close();
		pf_mutex.unlock();
	}
#endif
	set_program_list();
}

/**
    Call home to Server tells the server there hasn't been any communication.  When the Client starts up this thread treats it like the Client has never communicated with the Server,
	and begins calling home to tell the Server it exists.  The Server will respond with the appropriate communication.  This thread will routinely check the last known communication with
	the Server, and if it reaches a specified threshold, it will call home again.
**/
void call_home_task() /** FINISHED AND TESTED **/
{
	while(true)
	{
		/* Get current timestamp */
		time_t current_time = time(NULL);

		/* If current_time is more than 10 minutes since last known server communication OR the client is starting for the first time, then call home
			which will cause the Server to add the client to the list of machines being monitored if it is not already on it, and cause the Server to send a
			fresh program file containing the up to date list of programs being monitored.
		*/
		if( (current_time - LAST_SERVER_COMMUNICATION) > ( CALL_HOME*60) )
		{
			try{
				boost::asio::io_service io_service;
				boost::asio::ip::tcp::socket socket(io_service);
				boost::system::error_code error = boost::asio::error::host_not_found;
				socket.connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(SERVER_ADDRESS), SEND_PORT), error);

				// Send info flag
				//std::string flag = "0"; // Message 0 -> tells the Server that this Client needs a new Program File
				std::string flag = "1"; // Message 1 -> tells the Server that this Client has not been contacted yet
#ifdef __linux__
				char mname[1024]; // gethostname() returns a null terminated string so we don't need to worry about the length and overflows
				gethostname(mname, 31);
				std::string os = "2";
#endif
#ifdef _WIN32
				char mname[1024];
				DWORD dwCompNameLen = 1024;
				GetComputerName(mname, &dwCompNameLen);
				std::string os = "1";
#endif
				std::string message = flag + os + std::string(mname);

				/* If the server is not responding then the bytes sent will be 0, so loop, with a small delay, until the server responds because all other communication attempts with the Server
				are pointless if it isn't responding in the first place so it's acceptable to block within this loop */
				size_t sent = 0;
				sent = boost::asio::write(socket, boost::asio::buffer(message), boost::asio::transfer_all(), error);
				std::string output;

				char file[12] = {};
				size_t len = socket.read_some(boost::asio::buffer(file), error);
				while(len != 0)
				{
					file[len] = '\0';
					output += file;
					len = socket.read_some(boost::asio::buffer(file), error);
				}
				output = output.substr(1);

				socket.close();
				io_service.stop();
//std::cout << "Got: " << output << std::endl;
				/* Successfully called home, so reset last communication with Server */
				LAST_SERVER_COMMUNICATION = time(NULL);

				/* Write file */
std::cout << "CALLED HOME -- attempting to write file" << std::endl;
				write_program_file(output);
			}
			catch(std::exception &e)
			{
				time_t tt = time(NULL);
				struct tm tm;
				char buf[32];
				tm = *localtime(&tt);
				strftime(buf, 31, "%Y-%m-%d %H:%M:%S", &tm);
				std::ofstream fLog (ERR_LOG, std::ios::app);
				if(fLog.is_open())
				{
                    fLog << "Call_Home(): " << e.what() << " -- " << buf << "\n";
                }
				fLog.close();
			}
		}
		mSleep(1);
	}
}

/**
    Use this as the main loop
    - Check "Listener" is running, if not run it
    - Check "Gather" is running, if not run it
    - Check "Home" is running, if not run it
    - Report any crashes
**/
void run_tasks()
{
    while(true)
    {
		/* Check that Listening Thread is already running */
        if(!L_RUNNING)
        {
            boost::thread lt(linux_listen_thread);
            lt.detach();

			/* THREAD -- calls home if the Server has not contacted this Client within CONTACT_TIME timeframe */
			boost::threadpool::pool home(2);

			boost::threadpool::pool gather(2);

			/* THREAD -- displays message box */
			boost::threadpool::pool mbox(2);

			/* Run the thread pools */
			home.schedule(&call_home_task);
			gather.schedule(&gather_data);
			mbox.schedule(&display_windows_msgbox);
		}
    }
}

template<typename T>
std::vector<T> to_array(const std::string& s)
{
	std::vector<T> result;
	std::stringstream ss(s);
	std::string item;
	while(std::getline(ss, item, ',')) result.push_back(boost::lexical_cast<T>(item));
	return result;
}

/*** TODO!! -- need to add in exception checking to handle when config items are not found or mistyped in the file, and deal with the appropriate bad information later in execution ****/

int main(int ac, char **av)
{
    /* Set last server communication to be now-time */
    LAST_SERVER_COMMUNICATION = 0;

	/* Create directories if they don't already exist */
	create_directories(); /** Take a look at this further and make sure during INSTALL everything is setup the way you want, like config, child directories, etc !! **/

	ptree pt; // empty property tree object

    std::cout << "attempting to open config file.." << std::endl;
    try
    {
        // Load XML file into property tree.  If reading fails (for whatever reason) an exception is thrown so need to handle it
#ifdef __linux__
        read_xml("/opt/monitoring/config/default.cfg", pt);
#endif // __linux__
#ifdef _WIN32
        read_xml("C:\\Tools\\Monitoring\\Config\\default.cfg")
#endif // _WIN32

        // Read in File paths
        /** Possibly make this more robust later by adding in error checking / default settings.  Maybe based on where the program is compiled check child directories **/
        EVENT_FILE = (char*)pt.get<std::string>("path.data").c_str();
        ERR_LOG = (char*)pt.get<std::string>("path.log").c_str();
        P_FILE = (char*)pt.get<std::string>("path.plist").c_str(); /** Critical!!! ...missing / not found means the application cannot run **/
        CONFIG = (char*)pt.get<std::string>("path.config").c_str();

        // Network setup
        /** Critical section...if not set / found then application cannot run!! **/
        SERVER_ADDRESS = pt.get<std::string>("network.server");/** CRITICAL!!!! **/
        SEND_PORT = pt.get<int>("network.send_port", 16100);
        LISTEN_PORT = pt.get<int>("network.listen_port", 16200);

        // Messages setup
        /** Not a critical section persay... can use a very generic message default instead **/
        RESTRICTED_MSG = pt.get("messages.restricted", "The user account you are attempting to log in with is not allowed access on this machine.");
        EXPIRE_MSG = pt.get("messages.expire", "Your user accout will expire soon, and you will be logged out at that time.");
        EXPIRED_MSG = pt.get("messages.expired", "Your user account has expired and you will now be logged out.");

        // Read in a list of comma deliminated times representing how many minutes, before the expiration time, to display an expiration message to the user
        //  and convert those into a vector of integers
        std::vector<std::string> _v;
        std::string _s = pt.get("messages.display_times", "15,5,1");
        boost::algorithm::split(_v, _s, boost::algorithm::is_any_of(","), boost::algorithm::token_compress_on);
        BOOST_FOREACH(std::string s, _v)
        {
            int _i = atoi(s.c_str());
            MSG_DISPLAY_TIMES.push_back(_i);
        }

        // Settings setup
        EVENTSIZE = pt.get<int>("settings.size", 32);
        FREQUENCY = pt.get<int>("settings.frequency", 1); // Frequency to run the gathering portion, a multiple of 60 seconds
        CALL_HOME = pt.get<int>("settings.call_home", 10); // How many minutes to wait with no server contact before attempting to call home

        // Scripts setup
        std::map<std::string, std::string[7]> SCRIPTS;
        BOOST_FOREACH(ptree::value_type &v, pt.get_child("scripts"))
        {
            if(v.first == "script")
            {
                // Get the scripts command + path
                std::string _cmd = v.second.get<std::string>("command");

                std::string _times = v.second.get<std::string>("time", "notfound");
                if(_times != "notfound")
                {
                    std::vector<std::string> _v1;
                    std::string _weekdays[7] = {"2:00","2:00","2:00","2:00","2:00","2:00","2:00"};
                    boost::algorithm::split(_v1, _times, boost::algorithm::is_any_of(","), boost::algorithm::token_compress_on);
                    BOOST_FOREACH(std::string s, _v1)
                    {
                        std::vector<std::string> _v2;
                        boost::algorithm::split(_v2, s, boost::algorithm::is_any_of(":"), boost::algorithm::token_compress_on);

                        // Set the appropriate day in the weekday array to the time found (if any) for that day
                        _weekdays[atoi(_v2.at(0).c_str())] = (_v2.at(1) + ":" + _v2.at(2));
                    }

                    SCRIPTS.insert(std::pair<std::string, std::string[7]>(_cmd, _weekdays) );
                }
            }
        }
	}
	catch(std::exception &e)
	{
        std::cout << "Exception caught: " << e.what() << " with -- " << errno << std::endl;
        /*** Logging TODO ***/
        /** because certain config settings are required for this program to execute we cannot safely use default settings and continue execution **/
	}

	// Sync up with the local machines time and wait for a new minute to roll around so that data is gathered on the minute
	struct tm *tm_struct;
	time_t t = time(NULL);
	tm_struct = localtime(&t);
	int min = tm_struct->tm_sec;
	while(min > 1)
	{
		mSleep(1);
		t = time(NULL);
		tm_struct = localtime(&t);
		min = tm_struct->tm_sec;
	}

    run_tasks();

    return 0;
}
