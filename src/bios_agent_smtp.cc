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

// hack to allow reload of config file w/o the need to rewrite server to zloop and reactors
char *config_file = NULL;
zconfig_t *config = NULL;

void usage ()
{
    puts ("bios-agent-smtp [options]\n"
          "  -v|--verbose          verbose test output\n"
          "  -s|--server           smtp server name or address\n"
          "  -p|--port             smtp server port [25]\n"
          "  -u|--user             user for smtp authentication\n"
          "  -f|--from             mail from address\n"
          "  -e|--encryption       smtp encryption (none|tls|starttls) [none]\n"
          "  -c|--config           path to config file\n"
          "  -h|--help             print this information\n"
          "For security reasons, there is not option for password. Use environment variable.\n"
          "Environment variables for all paremeters are BIOS_SMTP_SERVER, BIOS_SMTP_PORT,\n"
          "BIOS_SMTP_USER, BIOS_SMTP_PASSWD, BIOS_SMTP_FROM and BIOS_SMTP_ENCRYPT\n"
          "Command line option takes precedence over variable.");
}


static int
s_timer_event (zloop_t *loop, int timer_id, void *output)
{
    static uint64_t last_check = zclock_mono ();
    uint64_t now = zclock_mono ();

    if ((now - last_check) > 5*60*1000) {
        zstr_send (output, "CHECK_NOW");
        last_check = zclock_mono ();
    }

    if (zconfig_has_changed (config))
        zstr_sendx (output, "LOAD", config_file, NULL);
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
    char *smsgateway   = getenv("BIOS_SMTP_SMS_GATEWAY");
    char *smtpverify   = getenv ("BIOS_SMTP_VERIFY_CA");

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
        {"config", required_argument, 0,'c'},
        {0, 0, 0, 0}
    };
    while(true) {

        int option_index = 0;
        c = getopt_long (argc, argv, "hvs:p:u:f:e:c:", long_options, &option_index);
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
    
    if (!config_file) {
        zsys_info ("No config file specified, falling back to enviromental variables.\nNote this is deprecated and will be removed!");
        config = zconfig_new ("root", NULL);
        zconfig_put (config, "server/verbose", verbose? "1" : "0");
        zconfig_put (config, "server/assets", "/var/lib/bios/agent-smtp/state");
        zconfig_put (config, "server/alerts", "/var/lib/bios/agent-smtp/state-alerts");

        zconfig_put (config, "smtp/server", smtpserver);
        zconfig_put (config, "smtp/port", smtpport ? smtpport : "25");
        if (smtpuser)
            zconfig_put (config, "smtp/user", smtpuser);
        if (smtppassword)
            zconfig_put (config, "smtp/password", smtppassword);
        if (smtpfrom)
            zconfig_put (config, "smtp/from", smtpfrom);
        zconfig_put (config, "smtp/encryption", smtpencrypt ? smtpencrypt : "none");
        if (msmtp_path)
            zconfig_put (config, "smtp/msmtppath", msmtp_path);
        if (smsgateway)
            zconfig_put (config, "smtp/smsgateway", smsgateway);
        zconfig_put (config, "smtp/verify_ca", smtpverify ? "1" : "0");

        zconfig_put (config, "malamute/endpoint", AGENT_SMTP_ENDPOINT);
        zconfig_put (config, "malamute/address", AGENT_SMTP_ADDRESS);
        zconfig_put (config, "malamute/consumers/ALERTS", ".*");
        zconfig_put (config, "malamute/consumer/ASSETS", ".*");
        zconfig_print (config);

        config_file = (char*) AGENT_SMTP_CONFIG_FILE;
        int r = zconfig_save (config, config_file);
        if (r == -1) {
            zsys_error ("Error while saving config file %s: %m", config_file);
            exit (EXIT_FAILURE);
        }
    }
    else {
        config = zconfig_load (config_file);
        if (!config) {
            zsys_error ("Failed to load config file %s: %m", config_file);
            exit (EXIT_FAILURE);
        }
    }

    puts ("START bios-agent-smtp - Daemon that is responsible for email notification about alerts");
    zactor_t *smtp_server = zactor_new (bios_smtp_server, (void *) NULL);
    if ( !smtp_server ) {
        zsys_error ("cannot start the daemon");
        return -1;
    }

    if (verbose)
        zstr_sendx (smtp_server, "VERBOSE", NULL);
    zstr_sendx (smtp_server, "LOAD", config_file, NULL);

    zloop_t *send_alert_trigger = zloop_new();
    // as 5 minutes is the smallest possible reaction time
    zloop_timer (send_alert_trigger, 1000, 0, s_timer_event, smtp_server);
    zloop_start (send_alert_trigger);

    zconfig_destroy (&config);
    zloop_destroy (&send_alert_trigger);
    zactor_destroy (&smtp_server);
    if (verbose) {
        zsys_info ("END bios_agent_smtp - Daemon that is responsible for email notification about alerts");
    }
    return 0;
}
