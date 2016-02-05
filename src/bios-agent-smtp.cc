/*  =========================================================================
    bios_alert_generator_server - Actor evaluating rules

    Copyright (C) 2014 - 2015 Eaton                                        
                                                                           
    This program is free software; you can redistribute it and/or modify   
    it under the terms of the GNU General Public License as published by   
    the Free Software Foundation; either version 2 of the License, or      
    (at your option) any later version.                                    
                                                                           
    This program is distributed in the hope that it will be useful,        
    but WITHOUT ANY WARRANTY; without even the implied warranty of         
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          
    GNU General Public License for more details.                           
                                                                           
    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.            
    =========================================================================
*/

/*
@header
    bios_smtp_server - Actor sending the emails about alerts
@discuss
@end
*/
#include "agent_smtp_classes.h"

static const char *PATH = "/var/lib/bios/agent-smtp";

// agents name
static const char *AGENT_NAME = "agent-smtp";

// malamute endpoint
static const char *ENDPOINT = "ipc://@/malamute";


int main (int argc, char** argv)
{
    bool verbose = false;

    puts ("START bios-agent-smtp - Daemon that is responsible for email notification about alerts");

    char* bios_log_level = getenv ("BIOS_LOG_LEVEL");
    if (argc == 2 && streq (argv[1], "-v")) {
        verbose = true;
    }
    else  if (bios_log_level && streq (bios_log_level, "LOG_DEBUG")) {
        verbose = true;
    }

    int argn;
    for (argn = 1; argn < argc; argn++) {
        if (streq (argv [argn], "--help")
        ||  streq (argv [argn], "-h")) {
            puts ("bios-agent-smtp [options] ...");
            puts ("  --verbose / -v         verbose test output");
            puts ("  --help / -h            this information");
            return 0;
        }
        else
        if (streq (argv [argn], "--verbose")
        ||  streq (argv [argn], "-v"))
            verbose = true;
        else {
            printf ("Unknown option: %s\n", argv [argn]);
            return 1;
        }
    }
    zactor_t *ag_server = zactor_new (bios_smtp_server, (void*) AGENT_NAME);
    if ( !ag_server ) {
        zsys_error ("cannot start the daemon");
        return -1;
    }

    if (verbose) {
        zstr_sendx (ag_server, "VERBOSE", NULL);
    }

    zstr_sendx (ag_server, "CONNECT", ENDPOINT, NULL);
    zstr_sendx (ag_server, "CONSUMER", "ALERTS", ".*", NULL);
    zstr_sendx (ag_server, "CONSUMER", "ASSETS", ".*", NULL);
    zstr_sendx (ag_server, "CONFIG", PATH, NULL);

    //  Accept and print any message back from server
    //  copy from src/malamute.c under MPL license
    while (true) {
        char *message = zstr_recv (ag_server);
        if (message) {
            puts (message);
            free (message);
        }
        else {
            puts ("interrupted");
            break;
        }
    }

    // TODO save info to persistence before I die
    zactor_destroy (&ag_server);
    if (verbose) {
        zsys_info ("END bios_agent_smtp - Daemon that is responsible for email notification about alerts");
    }
    return 0;
}
