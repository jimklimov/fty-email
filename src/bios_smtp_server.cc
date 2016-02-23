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

#include <algorithm>
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

#define SMTP_STATE_FILE "/var/bios/agent-smtp/state"

class ElementList 
{
 public:    
    ElementList() : _path(SMTP_STATE_FILE) {};
    ElementList(const std::string& path_to_file) : _path(path_to_file) {};

    // returns number of elements with key == asset_name (0 or 1) // TODO
    size_t  count (const std::string& asset_name) const;
    bool    isEmpty () const;
};

ElementList::count (const std::string& asset_name) const
{
    return _assets.count(asset_name);
}

ElementList::isEmpty ()
{
    return _assets.empty();
}

class ElementList {
public:

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

    // Path cannot be changed during the lifetime of the agent!
    // if you want allow users change it without killing the agent
    // you need to save old state to the new place to support the following situation:
    //
    // -------------------------------------------------------------------> t
    //      |                   |               |               |
    // agent_start_1            |               |          agent_start_2
    //                      path_change         |
    //                                      system_reboot
    //
    // Otherwise, at point agent_start_2 state would be read from the new place
    //  (but it is still empty)
    void setFile (const std::string &filePath)
    {
        _path = filePath;
    };


    /*
     * \brief Save to the file
     */
    int save (void) {
        std::ofstream ofs (_path + ".new", std::ofstream::out);
        if ( !ofs.good() ) {
            zsys_error ("Cannot open file '%s' for write", (_path + ".new").c_str());
            ofs.close();
            return -1;
        }
        int r = std::rename (std::string ( _path).append(".new").c_str (),
            std::string (_path.c_str ()).c_str());
        if ( r != 0 ) {
            zsys_error ("Cannot rename file '%s' to '%s'", _path.c_str(), _path.c_str());
            return -2;
        }
        ofs << serializeJSON();
        ofs.close();
        return 0;
    }

    /*
     * \brief load from  file
     */
    int load (void) {
        std::ifstream ifs (_path, std::ofstream::in);
        if ( !ifs.good() ) {
            zsys_error ("Cannot open file '%s' for read", _path.c_str());
            ifs.close();
            return -1;
        }
        try {
            cxxtools::SerializationInfo si;
            std::string json_string(std::istreambuf_iterator<char>(ifs), {});
            std::stringstream s(json_string);
        // TODO try
            cxxtools::JsonDeserializer json(s);
            json.deserialize(si);
            si >>= _assets;
            ifs.close();
            return 0;
        }
        catch ( const std::exception &e) {
            zsys_error ("Starting without initial state. Cannot deserialize the file '%s'. Error: '%s'", _path.c_str(), e.what());
            ifs.close();
            return -1;
        }
    }

    std::string serializeJSON (void) const
    {
        std::stringstream s;
        cxxtools::JsonSerializer js (s);
        js.beautify (true);
        js.serialize (_assets).finish();
        return s.str();
    };

private:

    /*
     * \brief Delete file
     *
     * \return 0 on success
     *         non-zero on error
     */
    int remove (void) {
        return std::remove (_path.c_str());
    };

    std::map <std::string, ElementDetails> _assets;

    std::string _path;
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
    if (!strstr (actions, "EMAIL"))
        return _alerts.end ();

    std::string ruleNameLower = std::string (ruleName);
    std::transform(ruleNameLower.begin(), ruleNameLower.end(), ruleNameLower.begin(), ::tolower);
    auto alertKey = std::make_pair(ruleNameLower, asset);

    // try to insert a new alert
    auto newAlert = _alerts.emplace(alertKey, AlertDescription (
            description,
            state,
            severity,
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
            ( it->second._severity != severity ) )
    {
        it->second._description = description;
        it->second._state = state;
        it->second._severity = severity;
        // important information changed -> need to notify asap
        it->second._lastUpdate = ::time(NULL);
    }
    else {
        // intentionally left empty
        // nothing changed -> inform according schedule
    }
    return it;
}

// TODO: make it configurable without recompiling
// If time is less 5 minutes, then email in some cases would be send aproximatly every 5 minutes,
// as some metrics are generated only once per 5 minute -> alert in 5 minuts -> email in 5 minuts
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
        zsys_error ("Not known interval");
        return 0;
    }
    else {
        zsys_error ("in %d [s]", it->second);
        return it->second - 60; 
        // BIOS-1802: time conflict with assumption:
        // if metric is computed it is send approximatly every 5 minutes +- X sec
    }
}


void AlertList::
    notify(
        const AlertsConfig::iterator it,
        const EmailConfiguration &emailConfiguration,
        const ElementList &elementList)
{
    if ( it == _alerts.cend() )
        return;
    //pid_t tmptmp = getpid();
    if ( emailConfiguration.isConfigured() ) {
        zsys_error("Mail system is not configured!");
        return;
    }
    // TODO function Need Notify()
    bool needNotify = false;

    auto &alertDescription = it->second;

    ElementDetails assetDetailes;
    try {
        assetDetailes = elementList.getElementDetails(it->first.second);
    }
    catch (const std::exception &e ) {
        zsys_error ("CAN'T NOTIFY unknown asset");
        return;
    }

    int64_t nowTimestamp = ::time(NULL);
    if ( alertDescription._lastUpdate > alertDescription._lastNotification ) {
        // Last notification was send BEFORE last
        // important change take place -> need to notify
        needNotify = true;
        zsys_debug ("important change -> notify");
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
            zsys_debug ("according schedule -> notify");
            if ( ( alertDescription._state == "ACK-PAUSE" ) || 
                    ( alertDescription._state == "ACK-IGNORE" ) ||
                    ( alertDescription._state == "ACK-SILENCE" ) ||
                    ( alertDescription._state == "RESOLVED" )
               ) {
                zsys_debug ("in this status we do not send emails");
            }
            else {
                needNotify = true;
            }
        }
    }

    if ( needNotify )
    {
        zsys_debug ("Want to notify");
        if ( assetDetailes._contactEmail.empty() )
        {
            zsys_error ("Can't send a notification. For the asset '%s' contact email is unknown",
                assetDetailes._name.c_str());
            return;
        }
        try {
            auto &ruleName = it->first.first;
            emailConfiguration._smtp.sendmail(
                assetDetailes._contactEmail,
                EmailConfiguration::generateSubject
                    (alertDescription, assetDetailes, ruleName),
                EmailConfiguration::generateBody
                    (alertDescription, assetDetailes, ruleName)
            );
            alertDescription._lastNotification = nowTimestamp;
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
    elementList.save();
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
            if (streq (cmd, "STATE_FILE_PATH")) {
                char* path = zmsg_popstr (msg);
                elementList.setFile (path);
                elementList.load();
                zstr_free (&path);
            }
            else
            if (streq (cmd, "SMTPCONFIG")) {
                char *param;
                // server
                param = zmsg_popstr (msg);
                if (param && param[0]) emailConfiguration.host(param);
                zstr_free (&param);
                // port
                param = zmsg_popstr (msg);
                if (param && param[0]) emailConfiguration.port(param);
                zstr_free (&param);
                // encryption
                param = zmsg_popstr (msg);
                if (param && param[0]) emailConfiguration.encryption(param);
                zstr_free (&param);
                // from
                param = zmsg_popstr (msg);
                if (param && param[0]) emailConfiguration.from(param);
                zstr_free (&param);
                // username
                param = zmsg_popstr (msg);
                if (param && param[0]) emailConfiguration.username(param);
                zstr_free (&param);
                // password
                param = zmsg_popstr (msg);
                if (param && param[0]) emailConfiguration.password(param);
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
        //  - an SMTP settings TODO
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
    // save info to persistence before I die
    elementList.save();
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

    // smtp server
    zactor_t *smtp_server = zactor_new (bios_smtp_server, (void*)"agent-smtp");
    zstr_sendx (smtp_server, "STATE_FILE_PATH", "kkk.xtx", NULL);
    if (verbose)
        zstr_send (smtp_server, "VERBOSE");
    zstr_sendx (smtp_server, "MSMTP_PATH", "src/btest", NULL);
    zstr_sendx (smtp_server, "CONNECT", endpoint, NULL);
    zsock_wait (smtp_server);
    zstr_sendx (smtp_server, "CONSUMER", "ASSETS",".*", NULL);
    zsock_wait (smtp_server);
    zstr_sendx (smtp_server, "CONSUMER", "ALERTS",".*", NULL);
    zsock_wait (smtp_server);

    mlm_client_t *alert_producer = mlm_client_new ();
    int rv = mlm_client_connect (alert_producer, endpoint, 1000, "alert_producer");
    assert( rv != -1 );
    rv = mlm_client_set_producer (alert_producer, "ALERTS");
    assert( rv != -1 );

    mlm_client_t *asset_producer = mlm_client_new ();
    rv = mlm_client_connect (asset_producer, endpoint, 1000, "asset_producer");
    assert( rv != -1 );
    rv = mlm_client_set_producer (asset_producer, "ASSETS");
    assert( rv != -1 );

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
    zmsg_t *msg = bios_proto_encode_asset (aux, asset_name, NULL, ext);
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
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
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
    msg = bios_proto_encode_asset (aux, asset_name3, NULL, ext);
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
    msg = bios_proto_encode_asset (aux, asset_name6, NULL, ext);
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
    msg = bios_proto_encode_asset (aux, asset_name6, NULL, ext);
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



    // clean up after the test
    mlm_client_destroy (&btest_reader);
    mlm_client_destroy (&asset_producer);
    mlm_client_destroy (&alert_producer);
    zactor_destroy (&smtp_server);
    zactor_destroy (&server);

    printf ("OK\n");
}
