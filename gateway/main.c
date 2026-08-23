#include "common.h"
#include "server.h"
#include "logger.h"
#include <stdio.h>


int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    struct server sv;

    if (server_init(&sv) < 0) {
        LOG_ERROR("server_init failed");
        return 1;
    }

    server_run(&sv);

    return 0;
}