#ifdef __linux__
#include "linux.h"
#endif
#ifdef _WIN32
#include "windows.h"
#endif

#include <pthread.h>
#include <string.h>

#define MAX_BUF 1024

using namespace std;

int main()
{
    int fd, tret;
    pthread_t threadID;
    bool LOGGED_IN = true;
    char * myfifo = (char*)"/tmp/fifo";

    /* open, read, and display the message from the FIFO */
    mkfifo(myfifo, S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
    fd = open(myfifo, O_RDONLY); // 0 == success, errno == error
    if(fd < 0)
    {
        printf("Error opening FIFO %s\n", strerror(errno));
        return -1;
    }
    else
    {
        printf("FD: %d and error: %s\n", fd, strerror(errno));
    }

    while(LOGGED_IN)
    {
        char buf[MAX_BUF];

        /*
            Check to see if the system is logged in, if not then exit
        */
        if(!is_logged_in())
            LOGGED_IN = false;

        /*
            Check to see if FIFO fd is valid, if not then re-open it
        */
        if(!is_valid_fd(fd))
        {
            fd = open(myfifo, O_RDONLY);
        }

        /* If no FIFO is open, then this will not block it will simply return EOF, however because the Client will start at an earlier
            run-level it is a given that the FIFO will already be open when this program is executed at startup.  O_NONBLOCK will not be set
            because we want read() to block.
        */
        int r = read(fd, buf, MAX_BUF); // 0 == success, errno == error
        if(r != 0)
        {
            buf[r] = '\0';
            tret = pthread_create(&threadID, NULL, display_msg, (void*)buf);
        }

        sleep(1); /**TEMPORARY until ready to implement, as having the Client running will force read() to block and prevent the need for this**/
    }
    close(fd); // 0 == success
    unlink(myfifo);

    return 0;
}
