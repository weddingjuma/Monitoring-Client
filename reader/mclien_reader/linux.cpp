#include "linux.h"

/*
    A fairly simple way of checking if the given file descriptor is valid.  Used to check if for some reason either the Client or System closed
    our FIFO for some reason, then we can reopen it for reading.
*/
int is_valid_fd(int fd)
{
    return fcntl(fd, F_GETFL) != -1 || errno != EBADF;
}

void display_msg(char *msg)
{
#ifdef __linux__

#endif
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

