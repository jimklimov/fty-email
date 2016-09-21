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
#include <getopt.h>

#include "agent_smtp_classes.h"

void usage ()
{
    puts ("Usage: bios-sendmail [-c|--config] [recipient ...]\n"
          "  -c|--config           path to config file\n"
          "Send email through bios-agent-smtp to given recipient.\n"
          "Email is read from stdin\n"
          "\n"
          "printf 'From:myself\nSubject:subject\n\nbody' | bios-sendmail joe@example.com\n");
}


int main (int argc, char** argv)
{

    int help = 0;
    // get options
    int c;
    struct option long_options[] =
    {
        {"help",       no_argument,       &help,    1},
        {"config", required_argument, 0,'c'},
        {0, 0, 0, 0}
    };

    char *config_file = NULL;

    while(true) {

        int option_index = 0;
        c = getopt_long (argc, argv, "c:", long_options, &option_index);
        if (c == -1) break;
        switch (c) {
        case 'c':
            config_file = optarg;
            break;
        case 0:
            // just now walking trough some long opt
            break;
        case 'h':
        default:
            help = 1;
            break;
        }
    }
    if (help) { usage(); exit(1); }
    // end of the options
    
    char *endpoint = strdup (AGENT_SMTP_ENDPOINT);
    char *smtp_address = strdup (AGENT_SMTP_ADDRESS);
    if (config_file) {
        zconfig_t *config = zconfig_load (config_file);
        if (!config) {
            zsys_error ("Failed to load %s: %m", config_file);
            exit (EXIT_FAILURE);
        }

        if (zconfig_get (config, "malamute/endpoint", NULL)) {
            zstr_free (&endpoint);
            endpoint = strdup (zconfig_get (config, "malamute/endpoint", NULL));
        }
        if (zconfig_get (config, "malamute/address", NULL)) {
            zstr_free (&smtp_address);
            smtp_address = strdup (zconfig_get (config, "malamute/address", NULL));
        }

        zconfig_destroy (&config);
    }

    mlm_client_t *client = mlm_client_new ();
    char *address = zsys_sprintf ("bios-sendmail.%d", getpid ());
    int r = mlm_client_connect (client, endpoint, 1000, address);
    zstr_free (&address);
    zstr_free (&endpoint);
    assert (r != -1);

    std::string body = read_all (STDIN_FILENO);
    mlm_client_sendtox (client, smtp_address, "SENDMAIL", body.c_str (), NULL);
    zstr_free (&smtp_address);
    zclock_sleep (256); // wait a bit for mlm_client to pass data

    mlm_client_destroy (&client);
    return 0;
}
