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

#include "agent_smtp_classes.h"

#include <iterator>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <malamute.h>
#include <bios_proto.h>
#include <math.h>
#include <functional>
#include "emailconfiguration.h"
#include <cxxtools/split.h>


class ElementList {
public:

    ElementList() {};

    size_t count (const std::string &assetName) const {
        return _assets.count(assetName);
    };

    bool empty () const {
        return _assets.empty();
    };

    // throws
    const ElementDetails& getElementDetails
        (const std::string &assetName) const
    {
        return _assets.at(assetName);
    };


    void setElementDetails (const ElementDetails &elementDetails)
    {
        auto assetName = elementDetails._name;
        auto it = _assets.find(assetName);
        if ( it == _assets.cend() ) {
            _assets.emplace (assetName, elementDetails);
        }
        else {
            it->second = elementDetails;
        }
    };
private:
    std::map <std::string, ElementDetails> _assets;
};

class AlertList {
public :
    AlertList(){};

    typedef typename std::map
        <std::pair<std::string, std::string>, AlertDescription> AlertsConfig;

    AlertsConfig::iterator add(
        const char *ruleName,
        const char *asset,
        const char *description,
        const char *state,
        const char *severity,
        int64_t timestamp,
        const char *actions);

    void notify(
        const AlertsConfig::iterator it,
        const EmailConfiguration &emailConfiguration,
        const ElementList &elementList);

private:
    // rule_name , asset_name  -> AlertDescription
    AlertsConfig _alerts;
};


AlertList::AlertsConfig::iterator AlertList::
    add(
        const char *ruleName,
        const char *asset,
        const char *description,
        const char *state,
        const char *severity,
        int64_t timestamp,
        const char *actions)
{
    auto alertKey = std::make_pair(ruleName, asset);
    std::set<std::string> actionList;
    std::set<std::string>::iterator actit = actionList.begin();
    cxxtools::split( '/', std::string(actions),
        std::inserter(actionList, actit) );

    // try to insert a new alert
    auto newAlert = _alerts.emplace(alertKey, AlertDescription (
            description,
            state,
            severity,
            actionList,
            timestamp
        ));
    // newAlert = pair (iterator, bool). true = inserted,
    // false = not inserted
    if ( newAlert.second == true ) {
        // it is the first time we see this alert
        // bacause in the map "alertKey" wasn't found
        return newAlert.first;
    }
    // it is not the first time we see this alert
    auto it = newAlert.first;
    if ( ( it->second._description != description ) ||
            ( it->second._state != state ) ||
            ( it->second._severity != severity ) ||
            ( it->second._actions != actionList ) ||
            ( it->second._timestamp != timestamp ) )
    {
        it->second._description = description;
        it->second._state = state;
        it->second._severity = severity;
        it->second._actions = actionList;
        it->second._timestamp = timestamp;
        // important information changed -> need to notify asap
        it->second._lastUpdate = ::time(NULL);
    }
    else {
        // intentionally left empty
        // nothing changed -> inform according schedule
    }
    return it;
}


static int
    getNotificationInterval(
        const std::string &severity,
        char priority)
{
    // According Aplha document (severity, priority)
    // is mapped onto the time interval [s]
    static const std::map < std::pair<std::string, char>, int> times = {
        { {"CRITICAL", '1'}, 5  * 60},
        { {"CRITICAL", '2'}, 15 * 60},
        { {"CRITICAL", '3'}, 15 * 60},
        { {"CRITICAL", '4'}, 15 * 60},
        { {"CRITICAL", '5'}, 15 * 60},
        { {"WARNING", '1'}, 1 * 60 * 60},
        { {"WARNING", '2'}, 4 * 60 * 60},
        { {"WARNING", '3'}, 4 * 60 * 60},
        { {"WARNING", '4'}, 4 * 60 * 60},
        { {"WARNING", '5'}, 4 * 60 * 60},
        { {"INFO", '1'}, 8 * 60 * 60},
        { {"INFO", '2'}, 24 * 60 * 60},
        { {"INFO", '3'}, 24 * 60 * 60},
        { {"INFO", '4'}, 24 * 60 * 60},
        { {"INFO", '5'}, 24 * 60 * 60}
    };
    auto it = times.find(std::make_pair (severity, priority));
    if ( it == times.cend() ) {
        return 0;
    }
    else {
        return it->second;
    }
}


void AlertList::
    notify(
        const AlertsConfig::iterator it,
        const EmailConfiguration &emailConfiguration,
        const ElementList &elementList)
{
    //pid_t tmptmp = getpid();
    if ( emailConfiguration.isConfigured() ) {
        zsys_error("Mail system is not configured!");
        return;
    }
    // TODO function Need Notify()
    bool needNotify = false;
    int64_t nowTimestamp = ::time(NULL);
    auto &ruleName = it->first.first;
    ElementDetails assetDetailes;
    try {
        assetDetailes = elementList.getElementDetails(it->first.second);
    }
    catch (const std::exception &e ) {
        zsys_error ("CAN'T NOTIFY unknown asset");
        return;
    }
    auto &alertDescription = it->second;
    if ( alertDescription._lastUpdate > alertDescription._lastNotification ) {
        // Last notification was send BEFORE last
        // important change take place -> need to notify
        needNotify = true;
    }
    else {
        // so, no important changes, but may be we need to
        // notify according the schedule
        if ( alertDescription._state == "RESOLVED" ) {
            // but only for active alerts
            needNotify = false;
        }
        else if ( ( nowTimestamp - alertDescription._lastNotification ) >
                    getNotificationInterval (alertDescription._severity,
                                             assetDetailes._priority)
                )
             // If  lastNotification + interval < NOW
        {
            // so, we found out that we need to notify according the schedule
            needNotify = true;
        }
    }

    if ( needNotify )
    {
        zsys_debug ("Want to notify");
        try {
            emailConfiguration._smtp.sendmail(
                assetDetailes._contactEmail,
                EmailConfiguration::generateBody
                    (alertDescription, assetDetailes, ruleName),
                EmailConfiguration::generateSubject
                    (alertDescription, assetDetailes, ruleName)
            );
            alertDescription._lastNotification = nowTimestamp;
            zsys_debug ("notification send at %d", nowTimestamp);
        }
        catch (const std::runtime_error& e) {
            zsys_error ("Error: %s", e.what());
            // here we'll handle the error
        }
    }
}


void onAlertReceive (
    bios_proto_t **message,
    AlertList &alertList,
    ElementList &elementList,
    EmailConfiguration &emailConfiguration)
{
    // when some alert message received
    bios_proto_t *messageAlert = *message;
    // check one more time to be sure, that it is an alert message
    if ( bios_proto_id (messageAlert) != BIOS_PROTO_ALERT )
    {
        zsys_error ("message bios_proto is not ALERT!");
        return;
    }
    // decode alert message
    const char *ruleName = bios_proto_rule (messageAlert);
    const char *asset = bios_proto_element_src (messageAlert);
    const char *description = bios_proto_description (messageAlert);
    const char *state = bios_proto_state (messageAlert);
    const char *severity = bios_proto_severity (messageAlert);
    int64_t timestamp = bios_proto_time (messageAlert);
    if( timestamp <= 0 ) {
        timestamp = time(NULL);
    }
    const char *actions = bios_proto_action (messageAlert);

    // 1. Find out information about the element
    if ( elementList.count(asset) == 0 ) {
        zsys_error ("no information is known about the asset, REQ-REP not implemented");
        // try to findout information about the asset
        // TODO
        return;
    }
    // information was found
    ElementDetails assetDetailes = elementList.getElementDetails (asset);

    // 2. add alert to the list of alerts
    auto it = alertList.add(
        ruleName,
        asset,
        description,
        state,
        severity,
        timestamp,
        actions);
    // notify user about alert
    alertList.notify(it, emailConfiguration, elementList);

    // destroy the message
    bios_proto_destroy (message);
}

void onAssetReceive (
    bios_proto_t **message,
    ElementList &elementList)
{
    // when some asset message received
    assert (message != NULL );

    bios_proto_t *messageAsset = *message;
    // check one more time to be sure, that it is an asset message
    if ( bios_proto_id (messageAsset) != BIOS_PROTO_ASSET )
    {
        zsys_error ("message bios_proto is not ASSET!");
        return;
    }
    // decode asset message
    // other fields in the message are not important
    const char *assetName = bios_proto_name (messageAsset);
    if ( assetName == NULL ) {
        zsys_error ("asset name is missing in the mesage");
        return;
    }

    zhash_t *aux = bios_proto_aux (messageAsset);
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

    // now, we need to get the contact information
    // TODO insert here a code to handle multiple contacts
    zhash_t *ext = bios_proto_ext (messageAsset);
    char *contact_name = NULL;
    char *contact_email = NULL;
    if ( ext != NULL ) {
        contact_name = (char *) zhash_lookup (ext, "contact_name");
        contact_email = (char *) zhash_lookup (ext, "contact_email");
    } else {
        zsys_error ("ext is missing");
    }


    ElementDetails newAsset;
    newAsset._priority = priority[0];
    newAsset._name = assetName;
    newAsset._contactName = ( contact_name == NULL ? "" : contact_name );
    newAsset._contactEmail = ( contact_email == NULL ? "" : contact_email );
    elementList.setElementDetails (newAsset);
    newAsset.print();

    // destroy the message
    bios_proto_destroy (message);
}

void
bios_smtp_server (zsock_t *pipe, void* args)
{
    bool verbose = false;

    char *name = strdup ((char*) args);

    mlm_client_t *client = mlm_client_new ();

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe(client), NULL);

    AlertList alertList;
    ElementList elementList;
    EmailConfiguration emailConfiguration;

    zsock_signal (pipe, 0);
    while (!zsys_interrupted) {

        void *which = zpoller_wait (poller, -1);
        if (!which)
            break;

        if (which == pipe) {
            zmsg_t *msg = zmsg_recv (pipe);
            char *cmd = zmsg_popstr (msg);
            zsys_debug ("actor command=%s", cmd);

            if (streq (cmd, "$TERM")) {
                zstr_free (&cmd);
                zmsg_destroy (&msg);
                goto exit;
            }
            else
            if (streq (cmd, "VERBOSE")) {
                verbose = true;
            }
            else
            if (streq (cmd, "CONNECT")) {
                char* endpoint = zmsg_popstr (msg);
                int rv = mlm_client_connect (client, endpoint, 1000, name);
                if (rv == -1) {
                    zsys_error ("%s: can't connect to malamute endpoint '%s'", name, endpoint);
                }
                zstr_free (&endpoint);
                zsock_signal (pipe, 0);
            }
            else
            if (streq (cmd, "PRODUCER")) {
                char* stream = zmsg_popstr (msg);
                int rv = mlm_client_set_producer (client, stream);
                if (rv == -1) {
                    zsys_error ("%s: can't set producer on stream '%s'", name, stream);
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
                    zsys_error ("%s: can't set consumer on stream '%s', '%s'", name, stream, pattern);
                }
                zstr_free (&pattern);
                zstr_free (&stream);
                zsock_signal (pipe, 0);
            }
            else
            if (streq (cmd, "MSMTP_PATH")) {
                char* path = zmsg_popstr (msg);
                emailConfiguration._smtp.msmtp_path (path);
                zstr_free (&path);
            }
            else
            if (streq (cmd, "CONFIG")) {
                //char* filename = zmsg_popstr (msg);
                // TODO read configuration SMTP
                /*
                */
            }
            else
            if (streq (cmd, "SMTPSERVER")) {
                char *host = zmsg_popstr (msg);
                if (host) emailConfiguration.host(host);
                zstr_free (&host);
            }
            else if (streq (cmd, "MSMTPCONFIG")) {
                char *config = zmsg_popstr (msg);
                if (config) emailConfiguration.config (config);
                zstr_free (&config);
            }
            else
            {
                zsys_info ("unhandled command %s", cmd);
            }
            zstr_free (&cmd);
            zmsg_destroy (&msg);
            continue;
        }

        // This agent is a reactive agent, it reacts only on messages
        // and doesn't do anything if there is no messages
        // TODO: probably email also should be send every XXX seconds,
        // even if no alerts were received
        zmsg_t *zmessage = mlm_client_recv (client);
        if ( zmessage == NULL ) {
            continue;
        }
        std::string topic = mlm_client_subject(client);
        if ( verbose ) {
            zsys_debug("Got message '%s'", topic.c_str());
        }
        // There are inputs
        //  - an alert from alert stream
        //  - an asset config message
        //  - an SMTP settings
        if( is_bios_proto (zmessage) ) {
            bios_proto_t *bmessage = bios_proto_decode (&zmessage);
            if( ! bmessage ) {
                zsys_error ("cannot decode bios_proto message, ignore it");
                continue;
            }
            if ( bios_proto_id (bmessage) == BIOS_PROTO_ALERT )  {
                onAlertReceive (&bmessage, alertList, elementList,
                    emailConfiguration);
            }
            else if ( bios_proto_id (bmessage) == BIOS_PROTO_ASSET )  {
                onAssetReceive (&bmessage, elementList);
            }
            else {
                zsys_error ("it is not an alert message, ignore it");
            }
            bios_proto_destroy (&bmessage);
        }
        zmsg_destroy (&zmessage);
    }
exit:
    // TODO save info to persistence before I die
    zpoller_destroy (&poller);
    mlm_client_destroy (&client);
    zstr_free (&name);
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
        fscanf (fp, "%d", &pid);
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

    // smtp server
    zactor_t *smtp_server = zactor_new (bios_smtp_server, (void*)"agent-smtp");
    if (verbose)
        zstr_send (smtp_server, "VERBOSE");
    zstr_sendx (smtp_server, "MSMTP_PATH", "src/btest", NULL);
    zstr_sendx (smtp_server, "CONNECT", endpoint, NULL);
    zsock_wait (smtp_server);
    zstr_sendx (smtp_server, "CONSUMER", "ASSETS",".*", NULL);
    zsock_wait (smtp_server);
    zstr_sendx (smtp_server, "CONSUMER", "ALERTS",".*", NULL);
    zsock_wait (smtp_server);


    // scenario 1:
    mlm_client_t *alert_producer = mlm_client_new ();
    int rv = mlm_client_connect (alert_producer, endpoint, 1000, "producer");
    assert( rv != -1 );
    rv = mlm_client_set_producer (alert_producer, "ALERTS");
    assert( rv != -1 );

    mlm_client_t *asset_producer = mlm_client_new ();
    rv = mlm_client_connect (asset_producer, endpoint, 1000, "asset_producer");
    assert( rv != -1 );
    rv = mlm_client_set_producer (asset_producer, "ASSETS");
    assert( rv != -1 );

    mlm_client_t *btest_reader = mlm_client_new ();
    rv = mlm_client_connect (btest_reader, endpoint, 1000, "btest-reader");
    assert( rv != -1 );

    // send asset info
    zhash_t *aux = zhash_new ();
    zhash_insert (aux, "priority", (void *)"1");
    const char *asset_name = "ASSET1";
    zmsg_t *msg = bios_proto_encode_asset (aux, asset_name, NULL, NULL);
    mlm_client_send (asset_producer, "Asset message1", &msg);
    zsys_info ("asset message was send");

    msg = bios_proto_encode_alert (NULL, "NY_RULE", asset_name, \
        "ACTIVE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    std::string atopic = "NY_RULE/CRITICAL@" + std::string (asset_name);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    zsys_info ("alert message was send");

    // read sent email
    msg = mlm_client_recv (btest_reader);
    zmsg_print (msg);
    zmsg_destroy (&msg);

    // send ack back, so btest can exit
    mlm_client_sendtox (
            btest_reader,
            mlm_client_sender (btest_reader),
            "BTEST-OK", "OK", NULL);
    zclock_sleep (1000);   //now we want to ensure btest calls mlm_client_destroy BEFORE we'll kill malamute

    mlm_client_destroy (&btest_reader);
    mlm_client_destroy (&asset_producer);
    mlm_client_destroy (&alert_producer);
    zactor_destroy (&smtp_server);
    zactor_destroy (&server);
    //  @selftest
    //  Simple create/destroy test
    //  @end
    printf ("OK\n");
}
