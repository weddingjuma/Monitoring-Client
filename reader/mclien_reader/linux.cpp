#include "linux.h"

/*
    A fairly simple way of checking if the given file descriptor is valid.  Used to check if for some reason either the Client or System closed
    our FIFO for some reason, then we can reopen it for reading.
*/
int is_valid_fd(int fd)
{
    return fcntl(fd, F_GETFL) != -1 || errno != EBADF;
}

/*
    Entry point pthread that will display a message to the user if they're logged into a graphical desktop.  It first checks to see if they're running
    KDE and uses kdialog.  Otherwise it defaults to X native message system xmessage.
*/
void *display_msg(void *msg)
{
    char *message = (char*)msg;
    char x[200];
    vector<string> olines;
	FILE *f;
    f = popen("pidof ksmserver", "r");
	while(fgets(x, 200, f) != NULL)
    {
        olines.push_back(x);
    }

    if(olines.size() > 0)
    {
        // kdialog
        string cmd = "kdialog --title \"Account Expiration Warning\" --warningcontinuecancel ";
        cmd += message;
        system(cmd.c_str());
    }
    else
    {
        // xmessage
        string cmd = "xmessage -title \"Account Expiration Warning\" -buttons ok -center ";
        cmd += message;
        system(cmd.c_str());
    }
}

/*
    Checks to make sure the machine is logged in.  Because this program belongs in Autostart, this part shouldn't be needed, but just in case
    this will ensure that the program exits when the user logs out and doesn't stay running.
*/
bool is_logged_in()
{
    char x[200];
    vector<string> olines;
	FILE *f;
    f = popen("who", "r");

	while(fgets(x, 200, f) != NULL)
    {
        if(string(x).find("(:0)") != string::npos)
        {
            pclose(f);
            return true;
        }
    }

    pclose(f);
    return false;
}

