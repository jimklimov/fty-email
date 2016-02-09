/*  =========================================================================
    btest - Testing binary

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
    btest - Testing binary
@discuss
@end
*/

#include "agent_smtp_classes.h"

int main (int argc, char *argv [])
{
    //keep it up to date with selftest
    static const char* endpoint = "ipc://bios-smtp-server-test";
    static const char* pidfile = "src/btest.pid";

    assert (!zsys_file_exists (pidfile));

    FILE *fp = fopen (pidfile, "w");
    assert (fp);
    fprintf (fp, "%d", getpid ());
    fflush (fp);
    fclose (fp);

    mlm_client_t *client = mlm_client_new ();
    assert (client);

    char *address;
    asprintf (&address, "btest-smtp.%" PRIi64, zclock_mono ());
    int r = mlm_client_connect (client, endpoint, 5000, address);
    assert (r != -1);

    zmsg_t *msg = zmsg_new ();
    assert (msg);
    for (int i = 0; i != argc; i++) {
        zmsg_addstr (msg, argv[i]);
    }

    auto stdin_s = read_all (STDIN_FILENO);
    zmsg_addstr (msg, stdin_s.c_str());

    r = mlm_client_sendto (client, "btest-reader", "btest", NULL, 1000, &msg);
    assert (r != -1);

    msg = mlm_client_recv (client);
    zmsg_print (msg);
    zmsg_destroy (&msg);

    zstr_free (&address);
    mlm_client_destroy (&client);
    unlink (pidfile);
    return 0;
}
