/*  =========================================================================
    bios_alert_generator_server - Actor evaluating rules

    Copyright (C) 2016 Eaton

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
    bios-sendmail - Send emails through agent-smtp "SENDMAIL" protocol
@discuss

    Usage:
    printf 'From:myself\nSubject:subject\n\nbody' | bios-sendmail joe@example.com\n

    Tools needs agent-smtp configured and running. See man bios_smtp_server and bios_agent_smtp

@end
*/
#include <getopt.h>

#include "agent_smtp_classes.h"

void usage ()
{
    puts ("Usage: bios-sendmail [options] addr < message\n"
          "  -c|--config           path to bios-agent-smtp config file\n"
          "  -s|--subject          mail subject\n"
          "  -a|--attachment       path to file to be attached to email\n"
          "Send email through bios-agent-smtp to given recipients in email body.\n"
          "Email body is read from stdin\n"
          "\n"
          "echo -e \"This is a testing email.\\n\\nyour team\" | bios-sendmail -s text -a ./myfile.tgz joe@example.com\n");
}

int main (int argc, char** argv)
{

    int help = 0;
    int verbose = 0;
    std::vector<std::string> attachments;
    const char *recipient = NULL;
    std::string subj;
    
    // get options
    int c;
    struct option long_options[] =
    {
        {"help",       no_argument,       &help,    1},
        {"verbose",    no_argument,       &verbose, 1},
        {"config",     required_argument, 0,'c'},
        {"subject",    required_argument, 0,'s'},
        {"attachment", required_argument, 0,'a'},
        {0, 0, 0, 0}
    };

    char *config_file = NULL;
    char *p = NULL;

    while(true) {

        int option_index = 0;
        c = getopt_long (argc, argv, "vc:s:a:", long_options, &option_index);
        if (c == -1) break;
        switch (c) {
        case 'c':
            config_file = optarg;
            break;
        case 'a':
            char path [PATH_MAX + 1];
            p = realpath (optarg, path);
            if (!p) {
                zsys_error ("Can't get absolute path for %s: %s", optarg, strerror (errno));
                exit (EXIT_FAILURE);
            }
            attachments.push_back (path);
            break;
        case 's':
            subj = optarg;
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
    if (optind < argc) {
        recipient = argv[optind];
        ++optind;
    }
    if (help || recipient == NULL || optind < argc) { usage(); exit(1); }
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
    assert (r != -1);
    if (verbose)
        zsys_debug ("bios-sendmail:\tendpoint=%s, address=%s, smtp_address=%s", endpoint, address, smtp_address);
    zstr_free (&address);
    zstr_free (&endpoint);
    assert (r != -1);

    std::string body = read_all (STDIN_FILENO);
    zmsg_t *mail = zmsg_new();
    // mail message suppose to be uuid/to/subj/body[/file1[/file2...]]
    zmsg_addstr (mail, "UUID");
    zmsg_addstr (mail, recipient);
    zmsg_addstr (mail, subj.c_str ());
    zmsg_addstr (mail, body.c_str());
    for (const auto file : attachments) {
        zmsg_addstr (mail, file.c_str());
    }
    
    r = mlm_client_sendto (client, smtp_address, "SENDMAIL", NULL, 2000, &mail);
    zstr_free (&smtp_address);
    if (r == -1) {
        zsys_error ("Failed to send the email (mlm_client_sendto returned -1).");
        zmsg_destroy (&mail);
        mlm_client_destroy (&client);
        
        exit (EXIT_FAILURE);
    }

    zmsg_t *msg = mlm_client_recv (client);

    char* uuid = zmsg_popstr (msg);
    char* code = zmsg_popstr (msg);
    char* reason = zmsg_popstr (msg);
    int exit_code = EXIT_SUCCESS;
    if (code[0] != '0')
        exit_code = EXIT_FAILURE;

    if (exit_code == EXIT_FAILURE || verbose)
        zsys_debug ("subject: %s, \ncode: %s \nreason: %s", mlm_client_subject (client), code, reason);
        
    zstr_free(&code);
    zstr_free(&reason);
    zstr_free(&uuid);
    mlm_client_destroy (&client);

    exit (exit_code);
}
