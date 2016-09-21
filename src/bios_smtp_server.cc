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

#include <cxxtools/serializationinfo.h>
#include <cxxtools/jsondeserializer.h>
#include <cxxtools/jsonserializer.h>
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

static bool isDelete(const char* operation) {
    if ( streq(operation, "delete" ) )
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

static bool
s_need_to_notify (alerts_map_iterator it,
          const Element& element,
          uint64_t &last_notification,
          uint64_t nowTimestamp
          )
{
    zsys_debug1 ("last_update = '%ld'\tlast_notification = '%ld'", it->second.last_update, last_notification);
    if (it->second.last_update > last_notification) {
        // Last notification was send BEFORE last
        // important change take place -> need to notify
        zsys_debug1 ("important change -> notify");
        return true;
    }
    // so, no important changes, but may be we need to
    // notify according the schedule
    if ( it->second.state == "RESOLVED" ) {
        // but only for resolved alerts
        return false;
    }
    if ((nowTimestamp - last_notification) > s_getNotificationInterval (it->second.severity, element.priority))
        // If  lastNotification + interval < NOW
    {
        // so, we found out that we need to notify according the schedule
        if (    it->second.state == "ACK-PAUSE"
             || it->second.state == "ACK-IGNORE"
             || it->second.state == "ACK-SILENCE"
             || it->second.state == "RESOLVED")
        {
            zsys_debug1 ("in this status we do not send emails");
            return false;
        }
        zsys_debug1 ("according schedule -> notify");
        return true;
    }
    return false;
}


static void
s_notify_base (alerts_map_iterator it,
          Smtp& smtp,
          const Element& element,
          const std::string& to,
          uint64_t &last_notification
          )
{
    uint64_t nowTimestamp = ::time (NULL);
    if ( !s_need_to_notify (it, element, last_notification, nowTimestamp) ) {
        // no notification is needed
        return;
    }
    zsys_debug1 ("Want to notify");
    if (to.empty ()) {
        zsys_debug1 ("Can't send a notification. For the asset '%s' contact email or sms_email is unknown", element.name.c_str ());
        return;
    }

    try {
        smtp.sendmail(
                to,
                generate_subject (it->second, element),
                generate_body (it->second, element)
                );
        last_notification = nowTimestamp;
    }
    catch (const std::runtime_error& e) {
        zsys_error ("Error: %s", e.what());
        // here we'll handle the error
    }
}

static void
s_notify (alerts_map_iterator it,
          Smtp& smtp,
          const ElementList& elements)
{
    Element element;
    if (!elements.get (it->first.second, element)) {
        zsys_error ("CAN'T NOTIFY unknown asset");
        return;
    }
    if (it->second.action_email ())
        s_notify_base (
            it,
            smtp,
            element,
            element.email,
            it->second.last_email_notification
        );
    if (it->second.action_sms ()) {
        s_notify_base (
            it,
            smtp,
            element,
            element.sms_email,
            it->second.last_sms_notification
        );
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
        bios_proto_destroy (p_message);
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

    // add alert to the list of alerts
    if (  (strcasestr (actions, "EMAIL") == NULL)
       && (strcasestr (actions, "SMS") == NULL )) {
        // this means, that for this alert no "SMS/EMAIL" action
        // -> we are not interested in it;
        zsys_debug1 ("Email action (%s) is not specified -> smtp agent is not interested in this alert", actions);
        bios_proto_destroy (p_message);
        return;
    }
    // so, EMAIL is in action -> add to the list of alerts
    alerts_map_iterator search = alerts.find (std::make_pair (rule_name, asset));
    if ( search == alerts.end () ) {
        // such alert is not known -> insert
        bool inserted = false;
        // we need an iterator to the right element
        std::tie (search, inserted) = alerts.emplace (std::make_pair (std::make_pair (rule_name, asset),
                    Alert (message)));
        zsys_debug1 ("Not known alert->add");
    }
    else if (search->second.state != state ||
            search->second.severity != severity ||
            search->second.description != description)
    {
        // such alert is already known, update info about it
        search->second.state = state;
        search->second.severity = severity;
        search->second.description = description;
        search->second.time = (uint64_t) timestamp;
        search->second.last_update = ::time (NULL);
        zsys_debug1 ("Known alert->update");
    }
    // Find out information about the element
    if (!elements.exists (asset)) {
        zsys_error ("The asset '%s' is not known", asset);
        // TODO: find information about the asset REQ-REP
        bios_proto_destroy (p_message);
        return;
    }
    // So, asset is known, try to notify about it
    s_notify (search, smtp, elements);
    bios_proto_destroy (p_message);
}

void onAssetReceive (
    bios_proto_t **p_message,
    ElementList& elements,
    const char* sms_gateway,
    bool verbose)
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
    char *contact_phone = NULL;
    if ( ext != NULL ) {
        contact_name = (char *) zhash_lookup (ext, "contact_name");
        contact_email = (char *) zhash_lookup (ext, "contact_email");
        contact_phone = (char *) zhash_lookup (ext, "contact_phone");
    } else {
        zsys_debug1 ("ext for asset %s is missing", name);
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
        newAsset.phone = ( contact_phone == NULL ? "" : contact_phone );
        if (sms_gateway && contact_phone) {
            try {
                newAsset.sms_email = sms_email_address (sms_gateway, contact_phone);
            }
            catch ( const std::exception &e ) {
                zsys_error (e.what());
            }
        }
        elements.add (newAsset);
        if (verbose)
            newAsset.debug_print();
    } else if ( isPartialUpdate(operation) ) {
        zsys_debug1 ("asset name = %s", name);
        if ( contact_name ) {
            zsys_debug1 ("to update: contact_name = %s", contact_name);
            elements.updateContactName (name, contact_name);
        }
        if ( contact_email ) {
            zsys_debug1 ("to update: contact_email = %s", contact_email);
            elements.updateEmail (name, contact_email);
        }
        if ( contact_phone ) {
            zsys_debug1 ("to update: contact_phone = %s", contact_email);
            elements.updatePhone (name, contact_phone);
            if (sms_gateway) {
                try {
                    elements.updateSMSEmail (name, sms_email_address (sms_gateway, contact_phone));
                }
                catch ( const std::exception &e ) {
                   zsys_error (e.what());
                }
            }
        }
    } else if ( isDelete(operation) ) {
        zsys_debug1 ("Asset:delete: '%s'", name);
        elements.remove (name);
    }
    else {
        zsys_error ("unsupported operation '%s' on the asset, ignore it", operation);
    }

    elements.save();
    // destroy the message
    bios_proto_destroy (p_message);
}

static int
    load_alerts_state (
        alerts_map &alerts,
        const char *file)
{
    if ( file == NULL ) {
        zsys_warning ("state file for alerts is not set up, no state is persist");
        return 0;
    }
    std::ifstream ifs (file, std::ios::in);
    if ( !ifs.good() ) {
        zsys_error ("load_alerts: Cannot open file '%s' for read", file);
        ifs.close();
        return -1;
    }
    try {
        cxxtools::SerializationInfo si;
        std::string json_string(std::istreambuf_iterator<char>(ifs), {});
        std::stringstream s(json_string);
        cxxtools::JsonDeserializer json(s);
        json.deserialize(si);
        si >>= alerts;
        ifs.close();
        return 0;
    }
    catch ( const std::exception &e) {
        zsys_error ("Cannot deserialize the file '%s'. Error: '%s'", file, e.what());
        ifs.close();
        return -1;
    }
}

static int
    save_alerts_state (
        const alerts_map &alerts,
        const char* file)
{
    if ( file == NULL ) {
        zsys_warning ("state file for alerts is not set up, no state is persist");
        return 0;
    }

    std::ofstream ofs (std::string(file) + ".new", std::ofstream::out);
    if ( !ofs.good() ) {
        zsys_error ("Cannot open file '%s'.new for write", file);
        ofs.close();
        return -1;
    }
    std::stringstream s;
    cxxtools::JsonSerializer js (s);
    js.beautify (true);
    js.serialize (alerts).finish ();
    ofs << s.str();
    ofs.close();
    int r = std::rename (std::string (file).append(".new").c_str (),
        std::string (file).c_str());
    if ( r != 0 ) {
        zsys_error ("Cannot rename file '%s'.new to '%s'", file, file);
        return -2;
    }
    return 0;
}

void
bios_smtp_server (zsock_t *pipe, void* args)
{
    bool verbose = false;
    char* name = NULL;
    char *endpoint = NULL;
    char *test_reader_name = NULL;
    char *sms_gateway = NULL;

    mlm_client_t *test_client = NULL;
    mlm_client_t *client = mlm_client_new ();

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (client), NULL);

    char *alerts_state_file = NULL;
    alerts_map alerts;
    ElementList elements;
    Smtp smtp;

    std::set <std::tuple <std::string, std::string>> streams;
    bool producer = false;

    zsock_signal (pipe, 0);
    while ( !zsys_interrupted ) {

        void *which = zpoller_wait (poller, -1);
        if (which == pipe) {
            zmsg_t *msg = zmsg_recv (pipe);
            char *cmd = zmsg_popstr (msg);
            zsys_debug1 ("%s:\tactor command=%s", name, cmd);

            if (streq (cmd, "$TERM")) {
                zsys_info ("Got $TERM");
                zstr_free (&cmd);
                zmsg_destroy (&msg);
                break;
            }
            else
            if (streq (cmd, "LOAD")) {
                char * config_file = zmsg_popstr (msg);
                zsys_debug1 ("LOAD: %s", config_file);

                zconfig_t *config = zconfig_load (config_file);
                if (!config) {
                    zsys_error ("Failed to load config file %s", config_file);
                    zstr_free (&config_file);
                    zstr_free (&cmd);
                    break;
                }

                // VERBOSE
                if (streq (zconfig_get (config, "server/verbose", "0"), "1")) {
                    verbose = true;
                    agent_smtp_verbose = true;
                    zsys_debug1 ("server/verbose true");
                }
                else {
                    verbose = false;
                    agent_smtp_verbose = false;
                    zsys_debug1 ("server/verbose false");
                }
                // SMS_GATEWAY
                if (zconfig_get (config, "smtp/smsgateway", NULL)) {
                    sms_gateway = strdup (zconfig_get (config, "smtp/smsgateway", NULL));
                }
                // MSMTP_PATH
                if (zconfig_get (config, "smtp/msmtppath", NULL)) {
                    smtp.msmtp_path (zconfig_get (config, "smtp/msmtppath", NULL));
                }
                //STATE_FILE_PATH_ASSETS
                if (zconfig_get (config, "server/assets", NULL)) {
                    char *path = zconfig_get (config, "server/assets", NULL);
                    elements.setFile (path);
                    // NOTE1234: this implies, that sms_gateway should be specified before !
                    elements.load(sms_gateway?sms_gateway : "");
                }
                //STATE_FILE_PATH_ALERTS
                if (zconfig_get (config, "server/alerts", NULL)) {
                    alerts_state_file = strdup (zconfig_get (config, "server/alerts", NULL));
                    int r = load_alerts_state (alerts, alerts_state_file);
                    if ( r == 0 ) {
                        zsys_debug1 ("State(alerts) loaded successfully");
                    }
                    else {
                        zsys_warning ("State(alerts) is not loaded successfully. Starting with empty set");
                    }
                }

                // smtp
                if (zconfig_get (config, "smtp/server", NULL)) {
                    smtp.host (zconfig_get (config, "smtp/server", NULL));
                }
                if (zconfig_get (config, "smtp/port", NULL)) {
                    smtp.port (zconfig_get (config, "smtp/port", NULL));
                }
                if (zconfig_get (config, "smtp/encryption", NULL)) {

                    const char* encryption = zconfig_get (config, "smtp/encryption", NULL);
                    if (   strcasecmp (encryption, "none") == 0
                        || strcasecmp (encryption, "tls") == 0
                        || strcasecmp (encryption, "starttls") == 0)
                        smtp.encryption (encryption);
                    else
                        zsys_warning ("<smtp>: smtp/encryption has unknown value, got %s, expected (none|tls|starttls)", encryption);

                }
                if (zconfig_get (config, "smtp/user", NULL)) {
                    smtp.username (zconfig_get (config, "smtp/user", NULL));
                }
                if (zconfig_get (config, "smtp/password", NULL)) {
                    smtp.password (zconfig_get (config, "smtp/password", NULL));
                }
                if (zconfig_get (config, "smtp/from", NULL)) {
                    smtp.from (zconfig_get (config, "smtp/from", NULL));
                }
                // turn on verify_ca only if smtp/verify_ca is 1
                smtp.verify_ca (streq (zconfig_get (config, "smtp/verify_ca", "0"), "1"));

                // malamute
                if (zconfig_get (config, "malamute/verbose", NULL)) {
                    const char* foo = zconfig_get (config, "malamute/verbose", "0");
                    bool mlm_verbose = foo[0] == '1' ? true : false;
                    mlm_client_set_verbose (client, mlm_verbose);
                }
                if (!mlm_client_connected (client)) {
                    if (   zconfig_get (config, "malamute/endpoint", NULL)
                        && zconfig_get (config, "malamute/address", NULL)) {

                        endpoint = strdup (zconfig_get (config, "malamute/endpoint", NULL));
                        name = strdup (zconfig_get (config, "malamute/address", NULL));
                        uint32_t timeout = 1000;
                        sscanf ("%" SCNu32, zconfig_get (config, "malamute/timeout", "1000"), &timeout);

                        zsys_debug1 ("%s: mlm_client_connect (%s, %" PRIu32 ", %s)", name, endpoint, timeout, name);
                        int r = mlm_client_connect (client, endpoint, timeout, name);
                        if (r == -1)
                            zsys_debug1 ("%s: mlm_client_connect (%s, %" PRIu32 ", %s) = %d FAILED", name, endpoint, timeout, name, r);
                    }
                    else
                        zsys_warning ("<smtp>: malamute/endpoint or malamute/address not in configuration, NOT connected to the broker!");
                }

                if (zconfig_locate (config, "malamute/consumers")) {
                    if (mlm_client_connected (client)) {
                        zconfig_t *consumers = zconfig_locate (config, "malamute/consumers");
                        for (zconfig_t *child = zconfig_child (consumers);
                                        child != NULL;
                                        child = zconfig_next (child))
                        {
                            const char* stream = zconfig_name (child);
                            const char* pattern = zconfig_value (child);
                            zsys_debug1 ("stream/pattern=%s/%s", stream, pattern);

                            // check if we're already connected to not let replay log to explode :)
                            if (streams.count (std::make_tuple (stream, pattern)) == 1)
                                continue;

                            int r = mlm_client_set_consumer (client, stream, pattern);
                            if (r == -1)
                                zsys_warning ("<%s>: cannot subscribe on %s/%s", name, stream, pattern);
                            else
                                streams.insert (std::make_tuple (stream, pattern));
                        }
                    }
                    else
                        zsys_warning ("<smtp>: client is not connected to broker, can't subscribe to the stream!");
                }

                if (zconfig_get (config, "malamute/producer", NULL)) {
                    if (!mlm_client_connected (client))
                        zsys_warning ("<smtp>: client is not connected to broker, can't publish on the stream!");
                    else
                    if (!producer) {
                        const char* stream = zconfig_get (config, "malamute/producer", NULL);
                        int r = mlm_client_set_producer (
                                client,
                                stream);
                        if (r == -1)
                            zsys_warning ("%s: cannot publish on %s", name, stream);
                        else
                            producer = true;
                    }
                }

                zconfig_destroy (&config);
                zstr_free (&config_file);
            }
            else
            if (streq (cmd, "CHECK_NOW")) {
                s_notify_all (alerts, smtp, elements);
            }
            else
            if (streq (cmd, "_MSMTP_TEST")) {
                test_reader_name = zmsg_popstr (msg);
                test_client = mlm_client_new ();
                assert (test_client);
                assert (endpoint);
                int rv = mlm_client_connect (test_client, endpoint, 1000, "smtp-test-client");
                if (rv == -1) {
                    zsys_error ("%s\t:can't connect on test_client, endpoint=%s", name, endpoint);
                }
                std::function <void (const std::string &)> cb = \
                    [test_client, test_reader_name] (const std::string &data) {
                        mlm_client_sendtox (test_client, test_reader_name, "btest", data.c_str ());
                    };
                smtp.sendmail_set_test_fn (cb);
            }
            else
            {
                zsys_error ("unhandled command %s", cmd);
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

        if (streq (mlm_client_command (client), "MAILBOX DELIVER")) {

            if (topic == "SENDMAIL") {
                bool sent_ok = false;
                zmsg_t *reply = zmsg_new ();
                try {
                    if (zmsg_size (zmessage) == 3) {
                        char *to = zmsg_popstr (zmessage);
                        char *subject = zmsg_popstr (zmessage);
                        char *body = zmsg_popstr (zmessage);
                        smtp.sendmail (to, subject, body);
                        zstr_free (&body);
                        zstr_free (&subject);
                        zstr_free (&to);
                    }
                    else
                        if (zmsg_size (zmessage) == 1) {
                            char *body = zmsg_popstr (zmessage);
                            smtp.sendmail (body);
                            zstr_free (&body);
                        }
                        else
                            throw std::runtime_error ("Can't parse zmsg_t with size " + zmsg_size (zmessage));
                    zmsg_addstr (reply, "OK");
                    sent_ok = true;
                }
                catch (const std::runtime_error &re) {
                    sent_ok = false;
                    uint32_t code = static_cast <uint32_t> (msmtp_stderr2code (re.what ()));
                    zmsg_addstrf (reply, "%" PRIu32, code);
                    zmsg_addstr (reply, re.what ());
                }

                int r = mlm_client_sendto (
                        client,
                        mlm_client_sender (client),
                        sent_ok ? "SENDMAIL-OK" : "SENDMAIL-ERR",
                        NULL,
                        1000,
                        &reply);
                if (r == -1)
                    zsys_error ("Can't send a reply for SENDMAIL to %s", mlm_client_sender (client));
            }
            else
                zsys_warning ("Unknown subject %s", topic.c_str ());

            zmsg_destroy (&zmessage);
            continue;
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
                save_alerts_state (alerts, alerts_state_file);
            }
            else if (bios_proto_id (bmessage) == BIOS_PROTO_ASSET)  {
                onAssetReceive (&bmessage, elements, sms_gateway, verbose);
            }
            else {
                zsys_error ("it is not an alert message, ignore it");
            }
            bios_proto_destroy (&bmessage);
        }
        zmsg_destroy (&zmessage);
    }

    // save info to persistence before I die
    elements.save();
    save_alerts_state (alerts, alerts_state_file);
    zstr_free (&name);
    zstr_free (&endpoint);
    zstr_free (&test_reader_name);
    zstr_free (&alerts_state_file);
    zstr_free (&sms_gateway);
    zpoller_destroy (&poller);
    mlm_client_destroy (&client);
    mlm_client_destroy (&test_client);
    zclock_sleep(1000);
}


//  -------------------------------------------------------------------------
//

/*
 * \brief helper function, that creates an smpt server as it would be created
 *      in the real environment
 *
 *  \param[in] verbose - if function should produce debug information or not
 *  \param[in] endpoint - endpoint of malamute where to connect
 *  \param[in] assets_file - an absolute path to the "asset" state file
 *  \param[in] alerts_file - an absolute path to the "alert" state file
 *  \param[in] agent_name - what agent name should be registred in malamute
 *  \param[in] clear_assets - do we want to clear "asset" state file before
 *                      smpt agent will start
 *  \param[in] clear_alerts - do we want to clear "alert" state file before
 *                      smpt agent will start
 *  \return smtp gent actor
 */
static zactor_t* create_smtp_server (
    bool verbose,
    const char *endpoint,
    const char *assets_file,
    const char *alerts_file,
    const char *agent_name,
    bool clear_assets,
    bool clear_alerts
    )
{
    if ( clear_assets )
        std::remove (assets_file);
    if ( clear_alerts )
        std::remove (alerts_file);
    zactor_t *smtp_server = zactor_new (bios_smtp_server, NULL);
    assert ( smtp_server != NULL );
    zconfig_t *config = zconfig_new ("root", NULL);
    zconfig_put (config, "server/alerts", alerts_file);
    zconfig_put (config, "server/assets", assets_file);
    zconfig_put (config, "malamute/endpoint", endpoint);
    zconfig_put (config, "malamute/address", agent_name);
    zconfig_put (config, "malamute/consumers/ASSETS", ".*");
    zconfig_put (config, "malamute/consumers/ALERTS", ".*");
    zconfig_save (config, "src/smtp.cfg");
    zconfig_destroy (&config);
    if ( verbose )
        zstr_send (smtp_server, "VERBOSE");
    zstr_sendx (smtp_server, "LOAD", "src/smtp.cfg", NULL);
    zclock_sleep (1500);
    if ( verbose )
        zsys_info ("smtp server started");
    return smtp_server;
}

/*
 * \brief Helper function for asset message sending
 *
 *  \param[in] verbose - if function should produce debug information or not
 *  \param[in] producer - a client, that is  will publish ASSET message
 *                  according parameters
 *  \param[in] email - email for this asset (or null if not specified)
 *  \param[in] priority - priprity of the asset (or null if not specified)
 *  \param[in] contact - contact name of the asset (or null if not specified)
 *  \param[in] opearion - operation on the asset (mandatory)
 *  \param[in] asset_name - name of the assset (mandatory)
 */
static void s_send_asset_message (
    bool verbose,
    mlm_client_t *producer,
    const char *priority,
    const char *email,
    const char *contact,
    const char *operation,
    const char *asset_name,
    const char *phone = NULL)
{
    assert (operation);
    assert (asset_name);
    zhash_t *aux = zhash_new ();
    if ( priority )
        zhash_insert (aux, "priority", (void *)priority);
    zhash_t *ext = zhash_new ();
    if ( email )
        zhash_insert (ext, "contact_email", (void *)email);
    if ( contact )
        zhash_insert (ext, "contact_name", (void *)contact);
    if ( phone )
        zhash_insert (ext, "contact_phone", (void *)phone);
    zmsg_t *msg = bios_proto_encode_asset (aux, asset_name, operation, ext);
    assert (msg);
    int rv = mlm_client_send (producer, asset_name, &msg);
    assert ( rv == 0 );
    if ( verbose )
        zsys_info ("asset message was send");
    zhash_destroy (&aux);
    zhash_destroy (&ext);
}

void
test9 (bool verbose, const char *endpoint)
{
    // this test has its own maamte inside!!! -> own smtp server ans own alert_producer
    // test, that alert state file works correctly
    if ( verbose )
        zsys_info ("Scenario %s", __func__);
    // malamute broker
    zactor_t *server = zactor_new (mlm_server, (void*) "Malamute_test9");
    assert ( server != NULL );
    zstr_sendx (server, "BIND", endpoint, NULL);
    if ( verbose )
        zsys_info ("malamute started");

    // smtp server
    const char *alerts_file = "test9_alerts.xtx";
    const char *assets_file = "test9_assets.xtx";
    zactor_t *smtp_server = create_smtp_server (
        verbose, endpoint, assets_file, alerts_file, "agent-smtp-test9", true, true);

    // alert producer
    mlm_client_t *alert_producer = mlm_client_new ();
    int rv = mlm_client_connect (alert_producer, endpoint, 1000, "alert_producer_test9");
    assert ( rv != -1 );
    rv = mlm_client_set_producer (alert_producer, "ALERTS");
    assert ( rv != -1 );
    if ( verbose )
        zsys_info ("alert producer started");

    // this alert is supposed to be in the file,
    // as action EMAIL is specified
    zmsg_t *msg = bios_proto_encode_alert (NULL, "SOME_RULE", "SOME_ASSET", \
        "ACTIVE","CRITICAL","ASDFKLHJH", 123456, "SMS/EMAIL");
    assert (msg);
    rv = mlm_client_send (alert_producer, "nobody-cares", &msg);
    assert ( rv != -1 );
    if ( verbose )
        zsys_info ("alert message was send");

    // this alert is NOT supposed to be in the file,
    // as action EMAIL is NOT specified
    msg = bios_proto_encode_alert (NULL, "SOME_RULE1", "SOME_ASSET", \
        "ACTIVE","CRITICAL","ASDFKLHJH", 123456, "SMS");
    assert (msg);
    if ( verbose )
        zsys_info ("alert message was send");
    rv = mlm_client_send (alert_producer, "nobody-cares", &msg);
    assert ( rv != -1 );

    zclock_sleep (1000); // let smtp process messages
    alerts_map alerts;
    int r = load_alerts_state (alerts, alerts_file);
    assert ( r == 0 );
    assert ( alerts.size() == 1 );
    // rule name is internally changed to lowercase
    Alert a = alerts.at(std::make_pair("some_rule","SOME_ASSET"));
    assert ( a.rule == "some_rule" );
    assert ( a.element == "SOME_ASSET" );
    assert ( a.state == "ACTIVE" );
    assert ( a.severity == "CRITICAL" );
    assert ( a.description == "ASDFKLHJH" );
    assert ( a.time == 123456 );
    assert ( a.last_email_notification == 0 );
    assert ( a.last_update > 0 );

    // clean up after
    mlm_client_destroy (&alert_producer);
    zactor_destroy (&smtp_server);
    zactor_destroy (&server);
    std::remove (alerts_file);
    std::remove (assets_file);
}


void test10 (
    bool verbose,
    const char *endpoint,
    zactor_t *mlm_server,
    mlm_client_t *asset_producer
    )
{
    // test, that ASSET messages are processed correctly
    if ( verbose )
        zsys_info ("Scenario %s", __func__);
    // we want new smtp server with empty states
    static const char *assets_file = "test10_assets_file.txt";
    ElementList elements; // element list to load
    Element element; // one particular element to check

    zactor_t *smtp_server = create_smtp_server
        (verbose, endpoint, assets_file, "test10_alerts.txt", "smtp-10", true, true);

    // test10-1 (create NOT known asset)
    s_send_asset_message (verbose, asset_producer, "1", "scenario10.email@eaton.com",
        "scenario10 Support Eaton", "create", "ASSET_10_1", "somephone");
    zclock_sleep (1000); // give time to process the message
    elements.setFile (assets_file);
    elements.load("notimportant");
    assert ( elements.size() == 1 );
    assert ( elements.get ("ASSET_10_1", element) );
    assert ( element.name == "ASSET_10_1");
    assert ( element.priority == 1);
    assert ( element.email == "scenario10.email@eaton.com");
    assert ( element.contactName == "scenario10 Support Eaton");
    assert ( element.phone == "somephone");

    // test10-2 (update known asset )
    s_send_asset_message (verbose, asset_producer, "2", "scenario10.email2@eaton.com",
        "scenario10 Support Eaton", "update", "ASSET_10_1");
    zclock_sleep (1000); // give time to process the message
    elements.setFile (assets_file);
    elements.load("notimportant");
    assert ( elements.size() == 1 );
    assert ( elements.get ("ASSET_10_1", element) );
    assert ( element.name == "ASSET_10_1");
    assert ( element.priority == 2);
    assert ( element.email == "scenario10.email2@eaton.com");
    assert ( element.contactName == "scenario10 Support Eaton");

    // test10-3 (inventory known asset (without email))
    s_send_asset_message (verbose, asset_producer, NULL, NULL,
        "scenario102 Support Eaton", "inventory", "ASSET_10_1");
    zclock_sleep (1000); // give time to process the message
    elements.setFile (assets_file);
    elements.load("notimportant");
    assert ( elements.size() == 1 );
    assert ( elements.get ("ASSET_10_1", element) );
    assert ( element.name == "ASSET_10_1");
    assert ( element.priority == 2);
    assert ( element.email == "scenario10.email2@eaton.com");
    assert ( element.contactName == "scenario102 Support Eaton");

    // test10-4 (create ALREADY known asset)
    if ( verbose )
        zsys_info ("___________________________Test10-4_________________________________");
    s_send_asset_message (verbose, asset_producer, "1", "scenario10.email@eaton.com",
        "scenario10 Support Eaton", "create", "ASSET_10_1");
    zclock_sleep (1000); // give time to process the message
    elements.setFile (assets_file);
    elements.load("notimportant");
    assert ( elements.size() == 1 );
    assert ( elements.get ("ASSET_10_1", element) );
    assert ( element.name == "ASSET_10_1");
    assert ( element.priority == 1);
    assert ( element.email == "scenario10.email@eaton.com");
    assert ( element.contactName == "scenario10 Support Eaton");

    // test10-5 (update NOT known asset)
    if ( verbose )
        zsys_info ("___________________________Test10-5_________________________________");
    s_send_asset_message (verbose, asset_producer, "2", "scenario10.email2@eaton.com",
        "scenario10 Support Eaton", "update", "ASSET_10_2");
    zclock_sleep (1000); // give time to process the message
    elements.setFile (assets_file);
    elements.load("notimportant");
    assert ( elements.size() == 2 );
    assert ( elements.get ("ASSET_10_1", element) );
    assert ( element.name == "ASSET_10_1");
    assert ( element.priority == 1);
    assert ( element.email == "scenario10.email@eaton.com");
    assert ( element.contactName == "scenario10 Support Eaton");

    assert ( elements.get ("ASSET_10_2", element) );
    assert ( element.name == "ASSET_10_2");
    assert ( element.priority == 2);
    assert ( element.email == "scenario10.email2@eaton.com");
    assert ( element.contactName == "scenario10 Support Eaton");

    // test10-6 (inventory known asset (WITH email))
    // inventory doesn't update priority even if it is provided
    if ( verbose )
        zsys_info ("___________________________Test10-6_________________________________");
    s_send_asset_message (verbose, asset_producer, "3", "scenario10.email@eaton.com",
        "scenario103 Support Eaton", "inventory", "ASSET_10_1");
    zclock_sleep (1000); // give time to process the message
    elements.setFile (assets_file);
    elements.load("notimportant");
    assert ( elements.size() == 2 );
    assert ( elements.get ("ASSET_10_1", element) );
    assert ( element.name == "ASSET_10_1");
    assert ( element.priority == 1);
    assert ( element.email == "scenario10.email@eaton.com");
    assert ( element.contactName == "scenario103 Support Eaton");

    assert ( elements.get ("ASSET_10_2", element) );
    assert ( element.name == "ASSET_10_2");
    assert ( element.priority == 2);
    assert ( element.email == "scenario10.email2@eaton.com");
    assert ( element.contactName == "scenario10 Support Eaton");


    // test10-7 (inventory NOT known asset (WITH email))
    if ( verbose )
        zsys_info ("___________________________Test10-7_________________________________");
    s_send_asset_message (verbose, asset_producer, "3", "scenario103.email@eaton.com",
        "scenario103 Support Eaton", "inventory", "ASSET_10_3");
    zclock_sleep (1000); // give time to process the message
    elements.setFile (assets_file);
    elements.load("notimportant");
    if (elements.get ("ASSET_10_3", element)) {
        zsys_info("ASSET FOUND! %s", element.name.c_str() );
    } else {
            if ( verbose )   zsys_info("ASSET_10_3 NOT FOUND - AS EXPECTED when inventoring not known asset" );
    }

    assert ( elements.size() == 2 );
    assert ( elements.get ("ASSET_10_1", element) );
    assert ( element.name == "ASSET_10_1");
    assert ( element.priority == 1);
    assert ( element.email == "scenario10.email@eaton.com");
    assert ( element.contactName == "scenario103 Support Eaton");

    assert ( elements.get ("ASSET_10_2", element) );
    assert ( element.name == "ASSET_10_2");
    assert ( element.priority == 2);
    assert ( element.email == "scenario10.email2@eaton.com");
    assert ( element.contactName == "scenario10 Support Eaton");

    // test10-8 (inventory NOT known asset (WITHOUT email))
    if ( verbose )
        zsys_info ("___________________________Test10-8_________________________________");
    s_send_asset_message (verbose, asset_producer, NULL, NULL,
        "scenario104 Support Eaton", "inventory", "ASSET_10_4");
    zclock_sleep (1000); // give time to process the message
    elements.setFile (assets_file);
    elements.load("notimportant");
    if (elements.get ("ASSET_10_4", element)) {
        zsys_info("ASSET FOUND! %s", element.name.c_str() );
    } else {
            if ( verbose )   zsys_info("ASSET_10_4 NOT FOUND - AS EXPECTED when inventoring not known asset" );
    }
    assert ( elements.size() == 2 );
    assert ( elements.get ("ASSET_10_1", element) );
    assert ( element.name == "ASSET_10_1");
    assert ( element.priority == 1);
    assert ( element.email == "scenario10.email@eaton.com");
    assert ( element.contactName == "scenario103 Support Eaton");

    assert ( elements.get ("ASSET_10_2", element) );
    assert ( element.name == "ASSET_10_2");
    assert ( element.priority == 2);
    assert ( element.email == "scenario10.email2@eaton.com");
    assert ( element.contactName == "scenario10 Support Eaton");

    // test10-9 (unknown operation on asset: XXX))
    if ( verbose )
        zsys_info ("___________________________Test10-9_________________________________");
    s_send_asset_message (verbose, asset_producer, "5", "scenario105.email@eaton.com",
        "scenario105 Support Eaton", "unknown_operation", "ASSET_10_1");
    zclock_sleep (1000); // give time to process the message
    elements.setFile (assets_file);
    elements.load("notimportant");

    assert ( elements.size() == 2 );
    assert ( elements.get ("ASSET_10_1", element) );
    assert ( element.name == "ASSET_10_1");
    assert ( element.priority == 1);
    assert ( element.email == "scenario10.email@eaton.com");
    assert ( element.contactName == "scenario103 Support Eaton");

    assert ( elements.get ("ASSET_10_2", element) );
    assert ( element.name == "ASSET_10_2");
    assert ( element.priority == 2);
    assert ( element.email == "scenario10.email2@eaton.com");
    assert ( element.contactName == "scenario10 Support Eaton");
    if ( verbose )
        zsys_info ("________________________All tests passed____________________________");

    zactor_destroy (&smtp_server);
}

//  Self test of this class
void
bios_smtp_server_test (bool verbose)
{
    const char *alerts_file = "kkk_alerts.xtx";
    const char *assets_file = "kkk_assets.xtx";
    std::remove (alerts_file);
    std::remove (assets_file);
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
    static const char* endpoint = "inproc://bios-smtp-server-test";

    // malamute broker
    zactor_t *server = zactor_new (mlm_server, (void*) "Malamute");
    assert ( server != NULL );
    zstr_sendx (server, "BIND", endpoint, NULL);
    if ( verbose )
        zsys_info ("malamute started");
    // smtp server
    zactor_t *smtp_server = zactor_new (bios_smtp_server, NULL);
    assert ( smtp_server != NULL );

    zconfig_t *config = zconfig_new ("root", NULL);
    zconfig_put (config, "server/alerts", alerts_file);
    zconfig_put (config, "server/assets", assets_file);
    zconfig_put (config, "malamute/endpoint", endpoint);
    zconfig_put (config, "malamute/address", "agent-smtp");
    zconfig_put (config, "malamute/consumers/ASSETS", ".*");
    zconfig_put (config, "malamute/consumers/ALERTS", ".*");
    zconfig_save (config, "src/smtp.cfg");
    zconfig_destroy (&config);

    if (verbose)
        zstr_send (smtp_server, "VERBOSE");
    zstr_sendx (smtp_server, "LOAD", "src/smtp.cfg", NULL);
    zstr_sendx (smtp_server, "_MSMTP_TEST", "btest-reader", NULL);
    if ( verbose )
        zsys_info ("smtp server started");

    mlm_client_t *alert_producer = mlm_client_new ();
    int rv = mlm_client_connect (alert_producer, endpoint, 1000, "alert_producer");
    assert( rv != -1 );
    rv = mlm_client_set_producer (alert_producer, "ALERTS");
    assert( rv != -1 );
    if ( verbose )
        zsys_info ("alert producer started");

    mlm_client_t *asset_producer = mlm_client_new ();
    rv = mlm_client_connect (asset_producer, endpoint, 1000, "asset_producer");
    assert( rv != -1 );
    rv = mlm_client_set_producer (asset_producer, "ASSETS");
    assert( rv != -1 );
    if ( verbose )
        zsys_info ("asset producer started");

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
    if (verbose)
        zsys_info ("asset message was send");
    // Ensure, that malamute will deliver ASSET message before ALERT message
    zclock_sleep (1000);

    //      2. send alert message
    msg = bios_proto_encode_alert (NULL, "NY_RULE", asset_name, \
        "ACTIVE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    std::string atopic = "NY_RULE/CRITICAL@" + std::string (asset_name);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    if (verbose)
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

    if (verbose) {
        zsys_debug ("expectedBody =\n%s", expectedBody.c_str ());
        zsys_debug ("\n");
        zsys_debug ("newBody =\n%s", newBody.c_str ());
    }
    assert ( expectedBody.compare(newBody) == 0 );

    // scenario 2: send an alert on the unknown asset
    //      1. DO NOT send asset info
    const char *asset_name1 = "ASSET2";

    //      2. send alert message
    msg = bios_proto_encode_alert (NULL, "NY_RULE", asset_name1, \
        "ACTIVE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    std::string atopic1 = "NY_RULE/CRITICAL@" + std::string (asset_name1);
    mlm_client_send (alert_producer, atopic1.c_str(), &msg);
    if (verbose)
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
    if (verbose)
        zsys_info ("asset message was send");

    //      2. send alert message
    msg = bios_proto_encode_alert (NULL, "NY_RULE", asset_name3, \
        "ACTIVE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    std::string atopic3 = "NY_RULE/CRITICAL@" + std::string (asset_name3);
    mlm_client_send (alert_producer, atopic3.c_str(), &msg);
    if (verbose)
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
    if (verbose)
        zsys_info ("alert message was send");

    //      2. read the email generated for alert
    msg = mlm_client_recv (btest_reader);
    assert (msg);
    if ( verbose ) {
        zsys_debug ("parameters for the email:");
        zmsg_print (msg);
    }
    zmsg_destroy (&msg);

    //      4. send an alert on the already known asset
    msg = bios_proto_encode_alert (NULL, "Scenario4", asset_name, \
        "ACTIVE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    if (verbose)
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
    if (verbose)
        zsys_info ("alert message was send");

    //      2. No mail should be generated
    poller = zpoller_new (mlm_client_msgpipe(btest_reader), NULL);
    which = zpoller_wait (poller, 1000);
    assert ( which == NULL );
    if ( verbose ) {
        zsys_debug ("No email was sent: SUCCESS");
    }
    zpoller_destroy (&poller);


    // scenario 6 ===============================================
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

    // intentionally left formatting intact, so git blame will reffer to original author ;-)
    if (verbose) {
    zsys_debug (" scenario 7 ===============================================");
    // scenario 7:
    //      1. send an alert on the already known asset
    atopic = "Scenario7/CRITICAL@" + std::string (asset_name);
    msg = bios_proto_encode_alert (NULL, "Scenario7", asset_name, \
        "ACTIVE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    if (verbose)
        zsys_info ("alert message was send");

    //      2. read the email generated for alert
    msg = mlm_client_recv (btest_reader);
    assert (msg);
    if ( verbose ) {
        zsys_debug ("parameters for the email:");
        zmsg_print (msg);
    }
    zmsg_destroy (&msg);

    //      4. send an alert on the already known asset
    msg = bios_proto_encode_alert (NULL, "Scenario4", asset_name, \
        "ACK-SILENCE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    if (verbose)
        zsys_info ("alert message was send");

    //      5. read the email generated for alert
    msg = mlm_client_recv (btest_reader);
    assert (msg);
    if ( verbose ) {
        zsys_debug ("parameters for the email:");
        zmsg_print (msg);
    }
    zmsg_destroy (&msg);

    // wait for 5 minutes
    zclock_sleep (5*60*1000);

    //      7. send an alert again
    msg = bios_proto_encode_alert (NULL, "Scenario4", asset_name, \
        "ACK-SILENCE","CRITICAL","ASDFKLHJH", 123456, "EMAIL");
    assert (msg);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    if (verbose)
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
    }

    // scenario 8 ===============================================
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

    //      8. send alert message again third time
    msg = bios_proto_encode_alert (NULL, rule_name8, asset_name8, \
        "ACTIVE","WARNING","Default load in ups ROZ.UPS36 is high", ::time (NULL), "EMAIL");
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

    //test SENDMAIL
    rv = mlm_client_sendtox (alert_producer, "agent-smtp", "SENDMAIL", "foo@bar", "Subject", "body", NULL);
    assert (rv != -1);
    msg = mlm_client_recv (alert_producer);
    assert (streq (mlm_client_subject (alert_producer), "SENDMAIL-OK"));
    assert (zmsg_size (msg) == 1);
    char *ok = zmsg_popstr (msg);
    assert (streq (ok, "OK"));
    zstr_free (&ok);
    zmsg_destroy (&msg);

    //  this fixes the reported memcheck error
    msg = mlm_client_recv (btest_reader);
    if (verbose)
        zmsg_print (msg);
    zmsg_destroy (&msg);

    //MVY: this test leaks memory - in general it's a bad idea to publish
    //messages to broker without reading them :)
    //test9 (verbose, "ipc://bios-smtp-server-test9");
    test10 (verbose, endpoint, server, asset_producer);

    // clean up after the test
    zactor_destroy (&smtp_server);
    mlm_client_destroy (&btest_reader);
    mlm_client_destroy (&asset_producer);
    mlm_client_destroy (&alert_producer);
    zactor_destroy (&server);

    printf ("OK\n");
}
