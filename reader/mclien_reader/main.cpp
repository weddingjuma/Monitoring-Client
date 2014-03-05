#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string>
#include <vector>
#include <algorithm>

#define MAX_BUF 1024

using namespace std;

/*
    A fairly simple way of checking if the given file descriptor is valid.  Used to check if for some reason either the Client or System closed
    our FIFO for some reason, then we can reopen it for reading.
*/
int is_valid_fd(int fd)
{
    return fcntl(fd, F_GETFL) != -1 || errno != EBADF;
}

bool is_logged_in()
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
        if(string(x).find("(:0)") != string::npos)
        {
#ifdef __linux__
            pclose(f);
#endif
            return true;
        }
    }
#ifdef __linux__
    pclose(f);
#endif
    return false;
}

void display_msg(char *msg)
{
#ifdef __linux__

#endif
}

int main()
{
    int fd;
    bool LOGGED_IN = true;
    char * myfifo = (char*)"/tmp/mclientfifo";
    char buf[MAX_BUF];

    /* open, read, and display the message from the FIFO */
    printf("Starting to listen...\n");
    fd = open(myfifo, O_RDONLY); // 0 == success, errno == error

    while(LOGGED_IN)
    {
        /*
            Check to see if the system is logged in, if not then exit
        */
        if(!is_logged_in())
            LOGGED_IN = false;

        /*
            Check to see if FIFO fd is valid, if not then re-open it
        */
        if(!is_valid_fd(fd))
            fd = open(myfifo, O_RDONLY);

        /* If no FIFO is open, then this will not block it will simply return EOF, however because the Client will start at an earlier
            run-level it is a given that the FIFO will already be open when this program is executed at startup.  O_NONBLOCK will not be set
            because we want read() to block.
        */
        read(fd, buf, MAX_BUF); // 0 == success, errno == error
        printf("Received: %s\n", buf);

        sleep(1); /**TEMPORARY until ready to implement, as having the Client running will force read() to block and prevent the need for this**/
    }
    close(fd); // 0 == success

    return 0;
}
