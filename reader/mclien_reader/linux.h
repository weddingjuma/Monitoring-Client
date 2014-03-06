#ifndef LINUX_H_INCLUDED
#define LINUX_H_INCLUDED



#endif // LINUX_H_INCLUDED
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <algorithm>

using namespace std;

bool is_logged_in();
void display_msg();
int is_valid_fd(int fd);
