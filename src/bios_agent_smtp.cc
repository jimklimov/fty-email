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

// agent's name
// DO NOT CHANGE! as other agents can rely on this name
static const char *AGENT_NAME = "agent-smtp";

// malamute's endpoint
// TODO make endpoint configurable on the start of the agent
static const char *ENDPOINT = "ipc://@/malamute";

void usage ()
{
    puts ("bios-agent-smtp [options]\n"
          "  -v|--verbose          verbose test output\n"
          "  -s|--server           smtp server name or address\n"
          "  -p|--port             smtp server port [25]\n"
          "  -u|--user             user for smtp authentication\n"
          "  -f|--from             mail from address\n"
          "  -e|--encryption       smtp encryption (none|tls|starttls) [none]\n"
          "  -h|--help             print this information\n"
          "For security reasons, there is not option for password. Use environment variable.\n"
          "Environment variables for all paremeters are BIOS_SMTP_SERVER, BIOS_SMTP_PORT,\n"
          "BIOS_SMTP_USER, BIOS_SMTP_PASSWD, BIOS_SMTP_FROM and BIOS_SMTP_ENCRYPT\n"
          "Command line option takes precedence over variable.");
}


static int
s_timer_event (zloop_t *loop, int timer_id, void *output)
{
    zstr_send (output, "CHECK_NOW");
    return 0;
}

int main (int argc, char** argv)
{
    int verbose = 0;
    int help = 0;

    // set defauts
    char* bios_log_level = getenv ("BIOS_LOG_LEVEL");
    if (bios_log_level && streq (bios_log_level, "LOG_DEBUG")) {
        verbose = 1;
    }
    char *smtpserver   = getenv("BIOS_SMTP_SERVER");
    char *smtpport     = getenv("BIOS_SMTP_PORT");
    char *smtpuser     = getenv("BIOS_SMTP_USER");
    char *smtppassword = getenv("BIOS_SMTP_PASSWD");
    char *smtpfrom     = getenv("BIOS_SMTP_FROM");
    char *smtpencrypt  = getenv("BIOS_SMTP_ENCRYPT");
    char *msmtp_path   = getenv("_MSMTP_PATH_");

    // get options
    int c;
    struct option long_options[] =
    {
        {"help",       no_argument,       &help,    1},
        {"verbose",    no_argument,       &verbose, 1},
        {"server",     required_argument, 0,'s'},
        {"port",       required_argument, 0,'p'},
        {"user",       required_argument, 0,'u'},
        {"from",       required_argument, 0,'f'},
        {"encryption", required_argument, 0,'e'},
        {0, 0, 0, 0}
    };
    while(true) {

        int option_index = 0;
        c = getopt_long (argc, argv, "hvs:p:u:f:e:", long_options, &option_index);
        if (c == -1) break;
        switch (c) {
        case 'v':
            verbose = 1;
            break;
        case 's':
            smtpserver = optarg;
            break;
        case 'p':
            smtpport = optarg;
            break;
        case 'u':
            smtpuser = optarg;
            break;
        case 'f':
            smtpfrom = optarg;
            break;
        case 'e':
            smtpencrypt = optarg;
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

    puts ("START bios-agent-smtp - Daemon that is responsible for email notification about alerts");
    zactor_t *smtp_server = zactor_new (bios_smtp_server, (void *) NULL);
    if ( !smtp_server ) {
        zsys_error ("cannot start the daemon");
        return -1;
    }

    if (verbose) {
        zstr_sendx (smtp_server, "VERBOSE", NULL);
    }
    // ATTENTION: the path for the state should be set up before any network activity!
    // as it should load the state first!
    zstr_sendx (smtp_server, "STATE_FILE_PATH", "/var/lib/bios/agent-smtp/state", NULL);
    // configure email, before we start to receive alerts
    zstr_sendx (smtp_server,
                "SMTPCONFIG",
                smtpserver ? smtpserver : "",       // server
                smtpport ? smtpport : "25",         // port
                smtpencrypt ? smtpencrypt : "none", // encryption
                smtpfrom ? smtpfrom : "",           // mail from
                smtpuser,                           // smtp username
                smtppassword,                       // smtp password
                NULL);
    if (msmtp_path) {
        zsys_info ("using alternative msmtp binary: %s", msmtp_path);
        zstr_sendx (smtp_server, "MSMTP_PATH", msmtp_path);
    }
    // Connect to malamute
    zstr_sendx (smtp_server, "CONNECT", ENDPOINT, AGENT_NAME, NULL);
    zsock_wait (smtp_server);
    // Listen for all alerts
    zstr_sendx (smtp_server, "CONSUMER", "ALERTS", ".*", NULL);
    zsock_wait (smtp_server);
    // Listen for all assets
    zstr_sendx (smtp_server, "CONSUMER", "ASSETS", ".*", NULL);
    zsock_wait (smtp_server);

    zloop_t *send_alert_trigger = zloop_new();
    // as 5 minutes is the smallest possible reaction time 
    zloop_timer (send_alert_trigger, 5*60*1000, 0, s_timer_event, smtp_server);
    zloop_start (send_alert_trigger);
    
    zloop_destroy (&send_alert_trigger);
    zactor_destroy (&smtp_server);
    if (verbose) {
        zsys_info ("END bios_agent_smtp - Daemon that is responsible for email notification about alerts");
    }
    return 0;
}
