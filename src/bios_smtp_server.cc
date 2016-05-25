/*  =========================================================================
    bios_smtp_server - Smtp actor

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
    bios_smtp_server - Smtp actor
@discuss
@end
*/
int agent_smtp_verbose = 0;

#define zsys_debug1(...) \
    do { if (agent_smtp_verbose) zsys_debug (__VA_ARGS__); } while (0);

#include "agent_smtp_classes.h"

#include <iterator>
#include <map>
#include <set>
#include <vector>
#include <tuple>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <malamute.h>
#include <bios_proto.h>
#include <math.h>
#include <functional>
#include <cxxtools/split.h>

#include "email.h"
#include "emailconfiguration.h"

typedef std::map <std::pair<std::string, std::string>, Alert> alerts_map;
typedef alerts_map::iterator alerts_map_iterator;


static bool isNew(const char* operation) {
    if ( streq(operation,"create" ) )
        return true;
    else
        return false;
}

static bool isUpdate(const char* operation) {
    if ( streq(operation,"update" ) )
        return true;
    else
        return false;
}


static bool isPartialUpdate(const char* operation) {
    if ( streq(operation, "inventory" ) )
        return true;
    else
        return false;
}
// TODO: make it configurable without recompiling
// If time is less 5 minutes, then email in some cases would be send aproximatly every 5 minutes,
// as some metrics are generated only once per 5 minute -> alert in 5 minuts -> email in 5 minuts
static uint64_t
s_getNotificationInterval(
        const std::string &severity,
        uint8_t priority)
{
    // According Aplha document (severity, priority)
    // is mapped onto the time interval [s]
    static const std::map <std::pair <std::string, uint8_t>, uint32_t> times = {
        { {"CRITICAL", 1}, 5  * 60},
        { {"CRITICAL", 2}, 15 * 60},
        { {"CRITICAL", 3}, 15 * 60},
        { {"CRITICAL", 4}, 15 * 60},
        { {"CRITICAL", 5}, 15 * 60},
        { {"WARNING", 1}, 1 * 60 * 60},
        { {"WARNING", 2}, 4 * 60 * 60},
        { {"WARNING", 3}, 4 * 60 * 60},
        { {"WARNING", 4}, 4 * 60 * 60},
        { {"WARNING", 5}, 4 * 60 * 60},
        { {"INFO", 1}, 8 * 60 * 60},
        { {"INFO", 2}, 24 * 60 * 60},
        { {"INFO", 3}, 24 * 60 * 60},
        { {"INFO", 4}, 24 * 60 * 60},
        { {"INFO", 5}, 24 * 60 * 60}
    };
    auto it = times.find (std::make_pair (severity, priority));
    if (it == times.end ()) {
        zsys_error ("Not known interval for severity = '%s', priority '%d'", severity.c_str (), priority);
        return 0;
    }

    zsys_debug1 ("in %d [s]", it->second);
    return it->second - 60;
    // BIOS-1802: time conflict with assumption:
    // if metric is computed it is send approximatly every 5 minutes +- X sec
}

static void
s_notify (alerts_map_iterator it,
          Smtp& smtp,
          const ElementList& elements)
{
    bool needNotify = false;
    Element element;
    if (!elements.get (it->first.second, element)) {
        zsys_error ("CAN'T NOTIFY unknown asset");
        return;
    }

    uint64_t nowTimestamp = ::time (NULL);
    zsys_error ("last_update = '%ld'\tlast_notification = '%ld'", it->second.last_update, it->second.last_notification);
    if (it->second.last_update > it->second.last_notification) {
        // Last notification was send BEFORE last
        // important change take place -> need to notify
        needNotify = true;
        zsys_info ("important change -> notify");
    }
    else {
        // so, no important changes, but may be we need to
        // notify according the schedule
        if ( it->second.state == "RESOLVED" ) {
            // but only for active alerts
            needNotify = false;
        }
        else
        if (
        (nowTimestamp - it->second.last_notification) > s_getNotificationInterval (it->second.severity, element.priority))
            // If  lastNotification + interval < NOW
        {
            // so, we found out that we need to notify according the schedule
            zsys_debug1 ("according schedule -> notify");
            if (it->second.state == "ACK-PAUSE" ||
                it->second.state == "ACK-IGNORE" ||
                it->second.state == "ACK-SILENCE" ||
                it->second.state == "RESOLVED") {
                    zsys_debug1 ("in this status we do not send emails");
            }
            else {
                needNotify = true;
            }
        }
    }

    if (needNotify) {
        zsys_info ("Want to notify");
        if (element.email.empty()) {
            zsys_error ("Can't send a notification. For the asset '%s' contact email is unknown", element.name.c_str ());
            return;
        }

        try {
            smtp.sendmail(
                element.email,
                generate_subject (it->second, element),
                generate_body (it->second, element)
            );
            it->second.last_notification = nowTimestamp;
        }
        catch (const std::runtime_error& e) {
            zsys_error ("Error: %s", e.what());
            // here we'll handle the error
        }
    }
}

static void
    s_notify_all (
        alerts_map &alerts,
        Smtp& smtp,
        const ElementList& elements
    )
{
    for ( auto it = alerts.begin(); it!= alerts.end(); it++ ) {
        s_notify (it, smtp, elements);
    }
}


static void
s_onAlertReceive (
    bios_proto_t **p_message,
    alerts_map& alerts,
    ElementList& elements,
    Smtp& smtp)
{
    if (p_message == NULL) return;
    bios_proto_t *message = *p_message;
    if (bios_proto_id (message) != BIOS_PROTO_ALERT) {
        zsys_error ("bios_proto_id (message) != BIOS_PROTO_ALERT");
        return;
    }
    // decode alert message
    const char *rule = bios_proto_rule (message);
    std::string rule_name (rule);
    std::transform (rule_name.begin(), rule_name.end(), rule_name.begin(), ::tolower);
    const char *state = bios_proto_state (message);
    const char *severity = bios_proto_severity (message);
    const char *asset = bios_proto_element_src (message);
    const char *description = bios_proto_description (message);
    int64_t timestamp = bios_proto_time (message);
    if (timestamp <= 0) {
        timestamp = ::time (NULL);
    }
    const char *actions = bios_proto_action (message);

    // 1. Find out information about the element
    if (!elements.exists (asset)) {
        zsys_error ("No information is known about the asset");
        // TODO: find information about the asset (query another agent)
        return;
    }

    // 2. add alert to the list of alerts
    if (strcasestr (actions, "EMAIL")) {
        alerts_map_iterator search = alerts.find (std::make_pair (rule_name, asset));
        if (search == alerts.end ()) { // insert
            bool inserted;
            std::tie (search, inserted) = alerts.emplace (std::make_pair (std::make_pair (rule_name, asset),
                                                          Alert (message)));
            assert (search != alerts.end ());
        }
        else if (search->second.state != state ||
                 search->second.severity != severity ||
                 search->second.description != description) { // update
            search->second.state = state;
            search->second.severity = severity;
            search->second.description = description;
            search->second.time = (uint64_t) time;
            search->second.last_update = ::time (NULL);
        }
        s_notify (search, smtp, elements);
    }
    bios_proto_destroy (p_message);
}

void onAssetReceive (
    bios_proto_t **p_message,
    ElementList& elements)
{
    if (p_message == NULL) return;
    bios_proto_t *message = *p_message;
    if (bios_proto_id (message) != BIOS_PROTO_ASSET) {
        zsys_error ("bios_proto_id (message) != BIOS_PROTO_ASSET");
        return;
    }

    const char *name = bios_proto_name (message); // was asset
    if (name == NULL) {
        zsys_error ("bios_proto_name () returned NULL");
        return;
    }

    // now, we need to get the contact information
    // TODO insert here a code to handle multiple contacts
    zhash_t *ext = bios_proto_ext (message);
    char *contact_name = NULL;
    char *contact_email = NULL;
    if ( ext != NULL ) {
        contact_name = (char *) zhash_lookup (ext, "contact_name");
        contact_email = (char *) zhash_lookup (ext, "contact_email");
    } else {
        zsys_info ("ext is missing");
    }

    const char *operation = bios_proto_operation (message);
    if ( isNew (operation) || isUpdate(operation) ) {
        zhash_t *aux = bios_proto_aux (message);
        const char *default_priority = "5";
        const char *priority = default_priority;
        if ( aux != NULL ) {
            // if we have additional information
            priority = (char *) zhash_lookup (aux, "priority");
            if ( priority == NULL ) {
                // but information about priority is missing
                priority = default_priority;
            }
        }
        Element newAsset;
        newAsset.priority = std::stoul (priority);
        newAsset.name = name;
        newAsset.contactName = ( contact_name == NULL ? "" : contact_name );
        newAsset.email = ( contact_email == NULL ? "" : contact_email );
        elements.add (newAsset);
        newAsset.debug_print();
    } else if ( isPartialUpdate(operation) ) {
        zsys_info ("asset name = %s", name);
        if ( contact_name ) {
            zsys_info ("to update: contact_name = %s", contact_name);
            elements.updateContactName (name, contact_name);
        }
        if ( contact_email ) {
            zsys_info ("to update: contact_email = %s", contact_email);
            elements.updateEmail (name, contact_email);
        }
    } else {
        zsys_error ("unsupported operation '%s' on the asset, ignore it", operation);
    }

    elements.save();
    // destroy the message
    bios_proto_destroy (p_message);
}


void
bios_smtp_server (zsock_t *pipe, void* args)
{
    bool verbose = false;

    mlm_client_t *client = mlm_client_new ();

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (client), NULL);

    alerts_map alerts;
    ElementList elements;
    Smtp smtp;

    zsock_signal (pipe, 0);
    while ( !zsys_interrupted ) {

        void *which = zpoller_wait (poller, -1);

        if (which == pipe) {
            zmsg_t *msg = zmsg_recv (pipe);
            char *cmd = zmsg_popstr (msg);
            zsys_debug1 ("actor command=%s", cmd);

            if (streq (cmd, "$TERM")) {
                zstr_free (&cmd);
                zmsg_destroy (&msg);
                goto exit;
            }
            else
            if (streq (cmd, "VERBOSE")) {
                verbose = true;
                agent_smtp_verbose = true;
                zsys_debug1 ("VERBOSE received");
            }
            else
            if (streq (cmd, "CHECK_NOW")) {
                s_notify_all (alerts, smtp, elements);
            }
            else
            if (streq (cmd, "CONNECT")) {
                char* endpoint = zmsg_popstr (msg);
                char* name = zmsg_popstr (msg);
                int rv = mlm_client_connect (client, endpoint, 1000, name);
                if (rv == -1) {
                    zsys_error ("%s: can't connect to malamute endpoint '%s'", name, endpoint);
                }
                zstr_free (&endpoint);
                zstr_free (&name);
                zsock_signal (pipe, 0);
            }
            else
            if (streq (cmd, "PRODUCER")) {
                char* stream = zmsg_popstr (msg);
                int rv = mlm_client_set_producer (client, stream);
                if (rv == -1) {
                    zsys_error ("can't set producer on stream '%s'", stream);
                }
                zstr_free (&stream);
                zsock_signal (pipe, 0);
            }
            else
            if (streq (cmd, "CONSUMER")) {
                char* stream = zmsg_popstr (msg);
                char* pattern = zmsg_popstr (msg);
                int rv = mlm_client_set_consumer (client, stream, pattern);
                if (rv == -1) {
                    zsys_error ("can't set consumer on stream '%s', '%s'", stream, pattern);
                }
                zstr_free (&pattern);
                zstr_free (&stream);
                zsock_signal (pipe, 0);
            }
            else
            if (streq (cmd, "MSMTP_PATH")) {
                char* path = zmsg_popstr (msg);
                smtp.msmtp_path (path);
                zstr_free (&path);
            }
            else
            if (streq (cmd, "STATE_FILE_PATH_ASSETS")) {
                char* path = zmsg_popstr (msg);
                elements.setFile (path);
                elements.load();
                zstr_free (&path);
            }
            else
            if (streq (cmd, "STATE_FILE_PATH_ALERTS")) {
                char* path = zmsg_popstr (msg);
                // do the job
                zstr_free (&path);
            }
            else
            if (streq (cmd, "SMTPCONFIG")) {
                char *param;
                // server
                param = zmsg_popstr (msg);
                if (param && param[0]) smtp.host(param);
                zstr_free (&param);
                // port
                param = zmsg_popstr (msg);
                if (param && param[0]) smtp.port(param);
                zstr_free (&param);
                // encryption
                param = zmsg_popstr (msg);
                if (param && param[0]) smtp.encryption(param);
                zstr_free (&param);
                // from
                param = zmsg_popstr (msg);
                if (param && param[0]) smtp.from(param);
                zstr_free (&param);
                // username
                param = zmsg_popstr (msg);
                if (param && param[0]) smtp.username(param);
                zstr_free (&param);
                // password
                param = zmsg_popstr (msg);
                if (param && param[0]) smtp.password(param);
                zstr_free (&param);
            }
            else
            {
                zsys_info ("unhandled command %s", cmd);
            }
            zstr_free (&cmd);
            zmsg_destroy (&msg);
            continue;
        }

        zmsg_t *zmessage = mlm_client_recv (client);
        if ( zmessage == NULL ) {
            continue;
        }
        std::string topic = mlm_client_subject(client);
        if ( verbose ) {
            zsys_debug1("Got message '%s'", topic.c_str());
        }
        // There are inputs
        //  - an alert from alert stream
        //  - an asset config message
        //  - an SMTP settings TODO
        if (is_bios_proto (zmessage)) {
            bios_proto_t *bmessage = bios_proto_decode (&zmessage);
            if (!bmessage) {
                zsys_error ("cannot decode bios_proto message, ignore it");
                continue;
            }
            if (bios_proto_id (bmessage) == BIOS_PROTO_ALERT)  {
                s_onAlertReceive (&bmessage, alerts, elements, smtp);
            }
            else if (bios_proto_id (bmessage) == BIOS_PROTO_ASSET)  {
                onAssetReceive (&bmessage, elements);
            }
            else {
                zsys_error ("it is not an alert message, ignore it");
            }
            bios_proto_destroy (&bmessage);
        }
        zmsg_destroy (&zmessage);
    }
exit:
    // save info to persistence before I die
    elements.save();
    zpoller_destroy (&poller);
    mlm_client_destroy (&client);
}


//  -------------------------------------------------------------------------
//  Self test of this class

void
bios_smtp_server_test (bool verbose)
{
    printf (" * bios_smtp_server: ");
    static const char* pidfile = "src/btest.pid";
    if (zfile_exists (pidfile))
    {
        FILE *fp = fopen (pidfile, "r");
        assert (fp);
        int pid;
        int r = fscanf (fp, "%d", &pid);
        assert (r > 0); // make picky compilers happy
        fclose (fp);
        zsys_info ("about to kill -9 %d", pid);
        kill (pid, SIGKILL);
        unlink (pidfile);
    }

    //  @selftest
    static const char* endpoint = "ipc://bios-smtp-server-test";

    // malamute broker
    zactor_t *server = zactor_new (mlm_server, (void*) "Malamute");
    assert ( server != NULL );
    if (verbose)
        zstr_send (server, "VERBOSE");
    zstr_sendx (server, "BIND", endpoint, NULL);
    zsys_error ("malamute started");
      // smtp server
    zactor_t *smtp_server = zactor_new (bios_smtp_server, NULL);
    zstr_sendx (smtp_server, "STATE_FILE_PATH", "kkk.xtx", NULL);
    if (verbose)
        zstr_send (smtp_server, "VERBOSE");
    zstr_sendx (smtp_server, "MSMTP_PATH", "src/btest", NULL);
    zstr_sendx (smtp_server, "CONNECT", endpoint, "agent-smtp", NULL);
    zsock_wait (smtp_server);
    zstr_sendx (smtp_server, "CONSUMER", "ASSETS",".*", NULL);
    zsock_wait (smtp_server);
    zstr_sendx (smtp_server, "CONSUMER", "ALERTS",".*", NULL);
    zsock_wait (smtp_server);
    zsys_error ("alert generator started");

    mlm_client_t *alert_producer = mlm_client_new ();
    int rv = mlm_client_connect (alert_producer, endpoint, 1000, "alert_producer");
    assert( rv != -1 );
    rv = mlm_client_set_producer (alert_producer, "ALERTS");
    assert( rv != -1 );
    zsys_error ("alert producer started");

    mlm_client_t *asset_producer = mlm_client_new ();
    rv = mlm_client_connect (asset_producer, endpoint, 1000, "asset_producer");
    assert( rv != -1 );
    rv = mlm_client_set_producer (asset_producer, "ASSETS");
    assert( rv != -1 );
    zsys_error ("asset producer started");


    // name of the client should be the same as name in the btest.cc
    mlm_client_t *btest_reader = mlm_client_new ();
    rv = mlm_client_connect (btest_reader, endpoint, 1000, "btest-reader");
    assert( rv != -1 );

    // scenario 1: send asset + send an alert on the already known correct asset
    //      1. send asset info
    zhash_t *aux = zhash_new ();
    zhash_insert (aux, "priority", (void *)"1");
    zhash_t *ext = zhash_new ();
    zhash_insert (ext, "contact_email", (void *)"scenario1.email@eaton.com");
    zhash_insert (ext, "contact_name", (void *)"eaton Support team");
    const char *asset_name = "ASSET1";
    zmsg_t *msg = bios_proto_encode_asset (aux, asset_name, "create", ext);
    assert (msg);
    mlm_client_send (asset_producer, "Asset message1", &msg);
    zhash_destroy (&aux);
    zhash_destroy (&ext);
    zsys_info ("asset message was send");
    // Ensure, that malamute will deliver ASSET message before ALERT message
    zclock_sleep (1000);

    //      2. send alert message
    msg = bios_proto_encode_alert (NULL, "NY_RULE", asset_name, \
        "ACTIVE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    std::string atopic = "NY_RULE/CRITICAL@" + std::string (asset_name);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    zsys_info ("alert message was send");

    //      3. read the email generated for alert
    msg = mlm_client_recv (btest_reader);
    assert (msg);
    if ( verbose ) {
        zsys_debug ("parameters for the email:");
        zmsg_print (msg);
    }
    //      4. compare the email with expected output
    int fr_number = zmsg_size(msg);
    char *body = NULL;
    while ( fr_number > 0 ) {
        zstr_free(&body);
        body = zmsg_popstr(msg);
        fr_number--;
    }
    zmsg_destroy (&msg);
    if ( verbose ) {
        zsys_debug ("email itself:");
        zsys_debug ("%s", body);
    }
    std::string newBody = std::string (body);
    zstr_free(&body);
    std::size_t subject = newBody.find ("Subject:");
    std::size_t date = newBody.find ("Date:");
    // in the body there is a line with current date -> remove it
    newBody.replace (date, subject - date, "");
    // need to erase white spaces, because newLines in "body" are not "\n"
    newBody.erase(remove_if(newBody.begin(), newBody.end(), isspace), newBody.end());

    // expected string withoiut date
    std::string expectedBody = "From:bios@eaton.com\nTo: scenario1.email@eaton.com\nSubject: CRITICAL alert on ASSET1 from the rule ny_rule is active!\n\n"
    "In the system an alert was detected.\nSource rule: ny_rule\nAsset: ASSET1\nAlert priority: P1\nAlert severity: CRITICAL\n"
    "Alert description: ASDFKLHJH\nAlert state: ACTIVE\n";
    expectedBody.erase(remove_if(expectedBody.begin(), expectedBody.end(), isspace), expectedBody.end());

    zsys_debug ("expectedBody =\n%s", expectedBody.c_str ());
    zsys_debug ("\n");
    zsys_debug ("newBody =\n%s", newBody.c_str ());
    assert ( expectedBody.compare(newBody) == 0 );

    //      5. send ack back, so btest can exit
    mlm_client_sendtox (
            btest_reader,
            mlm_client_sender (btest_reader),
            "BTEST-OK", "OK", NULL);
    zclock_sleep (1500);   //now we want to ensure btest calls mlm_client_destroy

    // scenario 2: send an alert on the unknown asset
    //      1. DO NOT send asset info
    const char *asset_name1 = "ASSET2";

    //      2. send alert message
    msg = bios_proto_encode_alert (NULL, "NY_RULE", asset_name1, \
        "ACTIVE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    std::string atopic1 = "NY_RULE/CRITICAL@" + std::string (asset_name1);
    mlm_client_send (alert_producer, atopic1.c_str(), &msg);
    zsys_info ("alert message was send");

    //      3. No mail should be generated
    zpoller_t *poller = zpoller_new (mlm_client_msgpipe(btest_reader), NULL);
    void *which = zpoller_wait (poller, 1000);
    assert ( which == NULL );
    if ( verbose ) {
        zsys_debug ("No email was sent: SUCCESS");
    }
    zpoller_destroy (&poller);

    // scenario 3: send asset without email + send an alert on the already known asset
    //      1. send asset info
    aux = zhash_new ();
    zhash_insert (aux, "priority", (void *)"1");
    ext = zhash_new ();
    zhash_insert (ext, "contact_name", (void *)"eaton Support team");
    const char *asset_name3 = "ASSET2";
    msg = bios_proto_encode_asset (aux, asset_name3, "update", ext);
    assert (msg);
    mlm_client_send (asset_producer, "Asset message3", &msg);
    zhash_destroy (&aux);
    zhash_destroy (&ext);
    zsys_info ("asset message was send");

    //      2. send alert message
    msg = bios_proto_encode_alert (NULL, "NY_RULE", asset_name3, \
        "ACTIVE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    std::string atopic3 = "NY_RULE/CRITICAL@" + std::string (asset_name3);
    mlm_client_send (alert_producer, atopic3.c_str(), &msg);
    zsys_info ("alert message was send");

    //      3. No mail should be generated
    poller = zpoller_new (mlm_client_msgpipe(btest_reader), NULL);
    which = zpoller_wait (poller, 1000);
    assert ( which == NULL );
    if ( verbose ) {
        zsys_debug ("No email was sent: SUCCESS");
    }
    zpoller_destroy (&poller);

    // scenario 4:
    //      1. send an alert on the already known asset
    atopic = "Scenario4/CRITICAL@" + std::string (asset_name);
    msg = bios_proto_encode_alert (NULL, "Scenario4", asset_name, \
        "ACTIVE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    zsys_info ("alert message was send");

    //      2. read the email generated for alert
    msg = mlm_client_recv (btest_reader);
    assert (msg);
    if ( verbose ) {
        zsys_debug ("parameters for the email:");
        zmsg_print (msg);
    }
    zmsg_destroy (&msg);

    //      3. send ack back, so btest can exit
    mlm_client_sendtox (
            btest_reader,
            mlm_client_sender (btest_reader),
            "BTEST-OK", "OK", NULL);

    //      4. send an alert on the already known asset
    msg = bios_proto_encode_alert (NULL, "Scenario4", asset_name, \
        "ACTIVE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    zsys_info ("alert message was send");

    //      5. email should not be send (it doesn't satisfy the schedule
    poller = zpoller_new (mlm_client_msgpipe(btest_reader), NULL);
    which = zpoller_wait (poller, 1000);
    assert ( which == NULL );
    if ( verbose ) {
        zsys_debug ("No email was sent: SUCCESS");
    }
    zpoller_destroy (&poller);

    // scenario 5: alert without action "EMAIL"
    //      1. send alert message
    msg = bios_proto_encode_alert (NULL, "NY_RULE", asset_name3, \
        "ACTIVE","CRITICAL","ASDFKLHJH", 123456, "SMS");
    assert (msg);
    mlm_client_send (alert_producer, atopic3.c_str(), &msg);
    zsys_info ("alert message was send");

    //      2. No mail should be generated
    poller = zpoller_new (mlm_client_msgpipe(btest_reader), NULL);
    which = zpoller_wait (poller, 1000);
    assert ( which == NULL );
    if ( verbose ) {
        zsys_debug ("No email was sent: SUCCESS");
    }
    zpoller_destroy (&poller);


    zsys_debug (" scenario 6 ===============================================");
    //
    //------------------------------------------------------------------------------------------------> t
    //
    //  asset is known       alert comes    no email        asset_info        alert comes   email send
    // (without email)                                   updated with email

    const char *asset_name6 = "asset_6";
    const char *rule_name6 = "rule_name_6";
    std::string alert_topic6 = std::string(rule_name6) + "/CRITICAL@" + std::string (asset_name6);

    //      1. send asset info without email
    aux = zhash_new ();
    assert (aux);
    zhash_insert (aux, "priority", (void *)"1");
    ext = zhash_new ();
    assert (ext);
    msg = bios_proto_encode_asset (aux, asset_name6, "create", ext);
    assert (msg);
    rv = mlm_client_send (asset_producer, "Asset message6", &msg);
    assert ( rv != -1 );
    // Ensure, that malamute will deliver ASSET message before ALERT message
    zclock_sleep (1000);

    //      2. send alert message
    msg = bios_proto_encode_alert (NULL, rule_name6, asset_name6, \
        "ACTIVE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    rv = mlm_client_send (alert_producer, alert_topic6.c_str(), &msg);
    assert ( rv != -1 );

    //      3. No mail should be generated
    poller = zpoller_new (mlm_client_msgpipe (btest_reader), NULL);
    which = zpoller_wait (poller, 1000);
    assert ( which == NULL );
    if ( verbose ) {
        zsys_debug ("No email was sent: SUCCESS");
    }
    zpoller_destroy (&poller);
    zclock_sleep (1000);   //now we want to ensure btest calls mlm_client_destroy

    //      4. send asset info one more time, but with email
    zhash_insert (ext, "contact_email", (void *)"scenario6.email@eaton.com");
    msg = bios_proto_encode_asset (aux, asset_name6, "update", ext);
    assert (msg);
    rv = mlm_client_send (asset_producer, "Asset message6", &msg);
    assert ( rv != -1 );
    zhash_destroy (&aux);
    zhash_destroy (&ext);
    // Ensure, that malamute will deliver ASSET message before ALERT message
    zclock_sleep (1000);

    //      5. send alert message again
    msg = bios_proto_encode_alert (NULL, rule_name6, asset_name6, \
        "ACTIVE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    rv = mlm_client_send (alert_producer, alert_topic6.c_str(), &msg);
    assert ( rv != -1 );

    //      6. Email SHOULD be generated
    poller = zpoller_new (mlm_client_msgpipe (btest_reader), NULL);
    which = zpoller_wait (poller, 1000);
    assert ( which != NULL );
    if ( verbose ) {
        zsys_debug ("Email was sent: SUCCESS");
    }
    msg = mlm_client_recv (btest_reader);
    zpoller_destroy (&poller);
    assert (msg);

    //      7. compare the email with expected output
    fr_number = zmsg_size(msg);
    body = NULL;
    while ( fr_number > 0 ) {
        zstr_free(&body);
        body = zmsg_popstr(msg);
        fr_number--;
    }
    zmsg_destroy (&msg);
    if ( verbose ) {
        zsys_debug ("email itself:");
        zsys_debug ("%s", body);
    }
    newBody = std::string (body);
    zstr_free(&body);
    subject = newBody.find ("Subject:");
    date = newBody.find ("Date:");
    // in the body there is a line with current date -> remove it
    newBody.replace (date, subject - date, "");
    // need to erase white spaces, because newLines in "body" are not "\n"
    newBody.erase(remove_if(newBody.begin(), newBody.end(), isspace), newBody.end());

    // expected string withoiut date
    expectedBody = "From:bios@eaton.com\nTo: scenario6.email@eaton.com\nSubject: CRITICAL alert on asset_6 from the rule rule_name_6 is active!\n\n"
    "In the system an alert was detected.\nSource rule: rule_name_6\nAsset: asset_6\nAlert priority: P1\nAlert severity: CRITICAL\n"
    "Alert description: ASDFKLHJH\nAlert state: ACTIVE\n";
    expectedBody.erase(remove_if(expectedBody.begin(), expectedBody.end(), isspace), expectedBody.end());
    assert ( expectedBody.compare(newBody) == 0 );

     //      8. send ack back, so btest can exit
    mlm_client_sendtox (
            btest_reader,
            mlm_client_sender (btest_reader),
            "BTEST-OK", "OK", NULL);
    zclock_sleep (1500);   //now we want to ensure btest calls mlm_client_destroy

/* ACE: test is too long for make check, but it works
    zsys_debug (" scenario 7 ===============================================");
    // scenario 7:
    //      1. send an alert on the already known asset
    atopic = "Scenario7/CRITICAL@" + std::string (asset_name);
    msg = bios_proto_encode_alert (NULL, "Scenario7", asset_name, \
        "ACTIVE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    zsys_info ("alert message was send");

    //      2. read the email generated for alert
    msg = mlm_client_recv (btest_reader);
    assert (msg);
    if ( verbose ) {
        zsys_debug ("parameters for the email:");
        zmsg_print (msg);
    }
    zmsg_destroy (&msg);

    //      3. send ack back, so btest can exit
    mlm_client_sendtox (
            btest_reader,
            mlm_client_sender (btest_reader),
            "BTEST-OK", "OK", NULL);

    //      4. send an alert on the already known asset
    msg = bios_proto_encode_alert (NULL, "Scenario4", asset_name, \
        "ACK-SILENCE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    zsys_info ("alert message was send");
    
    //      5. read the email generated for alert
    msg = mlm_client_recv (btest_reader);
    assert (msg);
    if ( verbose ) {
        zsys_debug ("parameters for the email:");
        zmsg_print (msg);
    }
    zmsg_destroy (&msg);

    //      6. send ack back, so btest can exit
    mlm_client_sendtox (
            btest_reader,
            mlm_client_sender (btest_reader),
            "BTEST-OK", "OK", NULL);

    // wait for 5 minutes
    zclock_sleep (5*60*1000);
    
    //      7. send an alert again
    msg = bios_proto_encode_alert (NULL, "Scenario4", asset_name, \
        "ACK-SILENCE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    zsys_info ("alert message was send");

    //      8. email should not be send (it  in the state, where alerts are not being send)
    poller = zpoller_new (mlm_client_msgpipe(btest_reader), NULL);
    which = zpoller_wait (poller, 1000);
    assert ( which == NULL );
    if ( verbose ) {
        zsys_debug ("No email was sent: SUCCESS");
    }
    zpoller_destroy (&poller);
    zclock_sleep (1500);   //now we want to ensure btest calls mlm_client_destroy
*/

    zsys_debug (" scenario 8 ===============================================");
    //
    //-------------------------------------------------------------------------------------------------------------------------------------> t
    //
    //  asset is known       alert comes    no email        asset_info        alert comes   email send    alert comes (<5min)   email send
    // (without email)                                   updated with email

    const char *asset_name8 = "ROZ.UPS36";
    const char *rule_name8 = "rule_name_8";
    std::string alert_topic8 = std::string(rule_name8) + "/CRITICAL@" + std::string (asset_name8);

    //      1. send asset info without email
    aux = zhash_new ();
    assert (aux);
    zhash_insert (aux, "priority", (void *)"1");
    ext = zhash_new ();
    assert (ext);
    msg = bios_proto_encode_asset (aux, asset_name8, "create", ext);
    assert (msg);
    rv = mlm_client_send (asset_producer, "Asset message8", &msg);
    assert ( rv != -1 );
    // Ensure, that malamute will deliver ASSET message before ALERT message
    zclock_sleep (1000);

    //      2. send alert message
    msg = bios_proto_encode_alert (NULL, rule_name8, asset_name8, \
        "ACTIVE","WARNING","Default load in ups ROZ.UPS36 is high", ::time (NULL), "EMAIL/SMS");
    assert (msg);
    rv = mlm_client_send (alert_producer, alert_topic6.c_str(), &msg);
    assert ( rv != -1 );

    //      3. No mail should be generated
    poller = zpoller_new (mlm_client_msgpipe (btest_reader), NULL);
    which = zpoller_wait (poller, 1000);
    assert ( which == NULL );
    if ( verbose ) {
        zsys_debug ("No email was sent: SUCCESS");
    }
    zpoller_destroy (&poller);
    zclock_sleep (1000);   //now we want to ensure btest calls mlm_client_destroy

    //      4. send asset info one more time, but with email
    zhash_insert (ext, "contact_email", (void *)"scenario8.email@eaton.com");
    msg = bios_proto_encode_asset (aux, asset_name8, "update", ext);
    assert (msg);
    rv = mlm_client_send (asset_producer, "Asset message8", &msg);
    assert ( rv != -1 );
    zhash_destroy (&aux);
    zhash_destroy (&ext);
    // Ensure, that malamute will deliver ASSET message before ALERT message
    zclock_sleep (1000);

    //      5. send alert message again second
    msg = bios_proto_encode_alert (NULL, rule_name8, asset_name8, \
        "ACTIVE","WARNING","Default load in ups ROZ.UPS36 is high", ::time (NULL), "EMAIL/SMS");
    assert (msg);
    rv = mlm_client_send (alert_producer, alert_topic8.c_str(), &msg);
    assert ( rv != -1 );

    //      6. Email SHOULD be generated
    poller = zpoller_new (mlm_client_msgpipe (btest_reader), NULL);
    which = zpoller_wait (poller, 1000);
    assert ( which != NULL );
    if ( verbose ) {
        zsys_debug ("Email was sent: SUCCESS");
    }
    msg = mlm_client_recv (btest_reader);
    zpoller_destroy (&poller);
    assert (msg);

    zmsg_destroy (&msg);

    //      7. send ack back, so btest can exit
    mlm_client_sendtox (
            btest_reader,
            mlm_client_sender (btest_reader),
            "BTEST-OK", "OK", NULL);
    zclock_sleep (1500);   //now we want to ensure btest calls mlm_client_destroy

    //      8. send alert message again third time
    msg = bios_proto_encode_alert (NULL, rule_name8, asset_name8, \
        "ACTIVE","WARNING","Default load in ups ROZ.UPS36 is high", ::time (NULL), "EMAIL/SMS");
    assert (msg);
    rv = mlm_client_send (alert_producer, alert_topic8.c_str(), &msg);
    assert ( rv != -1 );

    //      9. Email SHOULD NOT be generated
    poller = zpoller_new (mlm_client_msgpipe (btest_reader), NULL);
    which = zpoller_wait (poller, 1000);
    assert ( which == NULL );
    if ( verbose ) {
        zsys_debug ("Email was NOT sent: SUCCESS");
    }
    zpoller_destroy (&poller);

    zclock_sleep (1500);   //now we want to ensure btest calls mlm_client_destroy


    // clean up after the test
    mlm_client_destroy (&btest_reader);
    mlm_client_destroy (&asset_producer);
    mlm_client_destroy (&alert_producer);
    zactor_destroy (&smtp_server);
    zactor_destroy (&server);

    printf ("OK\n");
}
