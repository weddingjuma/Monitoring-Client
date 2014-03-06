#ifdef __linux__
#include "linux.h"
#endif
#ifdef _WIN32
#include "windows.h"
#endif

#include <pthread.h>

#define MAX_BUF 1024

using namespace std;

int main()
{
    int fd, tret;
    pthread_t threadID;
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
        int r = read(fd, buf, MAX_BUF); // 0 == success, errno == error
        buf[sizeof(buf)/sizeof(buf[0])] = '\0';

        if(r == 0)
        {
            tret = pthread_create(&threadID, NULL, display_msg, (void*)buf);
        }
        //display_msg(buf);

        sleep(10); /**TEMPORARY until ready to implement, as having the Client running will force read() to block and prevent the need for this**/
    }
    close(fd); // 0 == success

    return 0;
}
