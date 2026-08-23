#include "common.h"
#include "server.h"
#include "logger.h"
#include <stdio.h>


int main(int argc, char* argv[])
{

    if (argc < 2) {
        LOG_ERROR("invalid arg");
        return -1;
    }
    struct server sv;
    char *path_cert  = argv[1];
    char *path_key = argv[2];
    if (server_init(&sv, path_cert, path_key) < 0) {
        LOG_ERROR("server_init failed");
        return 1;
    }

    server_run(&sv);

    return 0;
}