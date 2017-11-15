/*  =========================================================================
    fty_email_server - Email actor

    Copyright (C) 2014 - 2017 Eaton

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
    fty_email_server - Email actor
@discuss
@end
*/

int agent_smtp_verbose = true;

#define zsys_debug1(...) \
    do { if (agent_smtp_verbose) zsys_debug (__VA_ARGS__); } while (0);

#include "fty_email_classes.h"

#include <cxxtools/serializationinfo.h>
#include <cxxtools/jsondeserializer.h>
#include <cxxtools/jsonserializer.h>
#include <cxxtools/regex.h>
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
#include <fty_proto.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <functional>
#include <cxxtools/split.h>

#include "email.h"
#include "emailconfiguration.h"

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

static void
s_notify_base (
          Smtp& smtp,
          const Element& element,
          const std::string& to,
          fty_proto_t *alert
          )
{
    if (to.empty ()) {
        zsys_debug1 ("Can't send a notification. For the asset '%s' contact email or sms_email is unknown", element.name.c_str ());
        return;
    }

    smtp.sendmail(
                to,
                generate_subject (alert, element),
                generate_body (alert, element)
                );
}

static void
s_notify (
          Smtp& smtp,
          const ElementList& elements,
          fty_proto_t *alert)
{
    Element element;
    const char *asset_name = fty_proto_name (alert);
    if (!elements.get (asset_name, element)) {
        zsys_error ("CAN'T NOTIFY unknown asset");
        return;
    }
    const char *action = fty_proto_action (alert);
    if (streq (action, "EMAIL")) {
        s_notify_base (smtp, element, element.email, alert);
    }
    else if (streq (action, "SMS")) {
        s_notify_base (smtp, element, element.sms_email, alert);
    }
    else if (streq (action, "EMAIL/SMS")) {
        s_notify_base (smtp, element, element.email, alert);
        s_notify_base (smtp, element, element.sms_email, alert);
    }
}

void onAssetReceive (
    fty_proto_t **p_message,
    ElementList& elements,
    const char* sms_gateway,
    bool verbose)
{
    if (p_message == NULL) return;
    fty_proto_t *message = *p_message;
    if (fty_proto_id (message) != FTY_PROTO_ASSET) {
        zsys_error ("fty_proto_id (message) != FTY_PROTO_ASSET");
        return;
    }

    const char *name = fty_proto_name (message); // was asset
    if (name == NULL) {
        zsys_error ("fty_proto_name () returned NULL");
        return;
    }

    // now, we need to get the contact information
    // TODO insert here a code to handle multiple contacts
    zhash_t *ext = fty_proto_ext (message);
    char *contact_name = NULL;
    char *contact_email = NULL;
    char *contact_phone = NULL;
    char *extname = NULL;
    if ( ext != NULL ) {
        contact_name = (char *) zhash_lookup (ext, "contact_name");
        contact_email = (char *) zhash_lookup (ext, "contact_email");
        contact_phone = (char *) zhash_lookup (ext, "contact_phone");
        extname = (char *) zhash_lookup (ext, "name");
    } else {
        zsys_debug1 ("ext for asset %s is missing", name);
    }

    const char *operation = fty_proto_operation (message);
    if ( isNew (operation) || isUpdate(operation) ) {
        zhash_t *aux = fty_proto_aux (message);
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
        newAsset.extname = (extname == NULL ? name : extname);
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
        if ( extname ) {
            zsys_debug1 ("to update: extname = %s", extname);
            elements.updateExtName (name, extname);
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
    fty_proto_destroy (p_message);
}

// return dfl is item is NULL or empty string!!
// smtp
//  user
//  password = ""
//
// will be treated the same way
static const char*
s_get (zconfig_t *config, const char* key, const char* dfl) {
    assert (config);

    char *ret = zconfig_get (config, key, dfl);
    if (!ret || streq (ret, ""))
        return dfl;
    return ret;
}

zmsg_t *
fty_email_encode (
        const char *uuid,
        const char *to,
        const char *subject,
        zhash_t *headers,
        const char *body,
        ...)
{

    assert (uuid);
    assert (to);
    assert (subject);
    assert (body);

    zmsg_t *msg = zmsg_new ();
    if (!msg)
        return NULL;

    zmsg_addstr (msg, uuid);
    zmsg_addstr (msg, to);
    zmsg_addstr (msg, subject);
    zmsg_addstr (msg, body);

    if (!headers) {
        zhash_t *headers = zhash_new ();
        zframe_t *frame = zhash_pack(headers);
        zmsg_append (msg, &frame);
        zhash_destroy (&headers);
    }
    else {
        zframe_t *frame = zhash_pack(headers);
        zmsg_append (msg, &frame);
    }

    va_list args;
    va_start (args, body);
    const char* path = va_arg (args, const char*);

    while (path) {
        zmsg_addstr (msg, path);
        path = va_arg (args, const char*);
    }

    va_end (args);

    return msg;
}


void
fty_email_server (zsock_t *pipe, void* args)
{
    bool sendmail_only = (args && streq ((char*) args, "sendmail-only"));
    bool verbose = false;
    char* name = NULL;
    char *endpoint = NULL;
    char *test_reader_name = NULL;
    char *sms_gateway = NULL;

    mlm_client_t *test_client = NULL;
    mlm_client_t *client = mlm_client_new ();
    bool client_connected = false;

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (client), NULL);

    ElementList elements;
    Smtp smtp;

    std::set <std::tuple <std::string, std::string>> streams;
    bool producer = false;

    zsock_signal (pipe, 0);
    while ( !zsys_interrupted ) {

        void *which = zpoller_wait (poller, -1);

        if (which == pipe) {
            zsys_debug1 ("%s:\twhich == pipe", name);
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
            if (streq (cmd, "VERBOSE")) {
                verbose = true;
                agent_smtp_verbose = true;
                zstr_free (&cmd);
            }
            else
            if (streq (cmd, "LOAD")) {
                char * config_file = zmsg_popstr (msg);
                zsys_debug1 ("(agent-smtp):\tLOAD: %s", config_file);

                zconfig_t *config = zconfig_load (config_file);
                if (!config) {
                    zsys_error ("Failed to load config file %s", config_file);
                    zstr_free (&config_file);
                    zstr_free (&cmd);
                    break;
                }

                // VERBOSE
                if (streq (zconfig_get (config, "server/verbose", "false"), "true")) {
                    verbose = true;
                    agent_smtp_verbose = true;
                }
                else {
                    verbose = false;
                    agent_smtp_verbose = false;
                }
                // SMS_GATEWAY
                if (s_get (config, "smtp/smsgateway", NULL)) {
                    sms_gateway = strdup (s_get (config, "smtp/smsgateway", NULL));
                }
                // MSMTP_PATH
                if (s_get (config, "smtp/msmtppath", NULL)) {
                    smtp.msmtp_path (s_get (config, "smtp/msmtppath", NULL));
                }
                //STATE_FILE_PATH_ASSETS
                if (!sendmail_only) {
                    if (s_get (config, "server/assets", NULL)) {
                        const char *path = s_get (config, "server/assets", NULL);
                        elements.setFile (path);
                        // NOTE1234: this implies, that sms_gateway should be specified before !
                        elements.load(sms_gateway?sms_gateway : "");
                    }
                }

                // smtp
                if (s_get (config, "smtp/server", NULL)) {
                    smtp.host (s_get (config, "smtp/server", NULL));
                }
                if (s_get (config, "smtp/port", NULL)) {
                    smtp.port (s_get (config, "smtp/port", NULL));
                }

                const char* encryption = zconfig_get (config, "smtp/encryption", "NONE");
                if (   strcasecmp (encryption, "none") == 0
                    || strcasecmp (encryption, "tls") == 0
                    || strcasecmp (encryption, "starttls") == 0)
                    smtp.encryption (encryption);
                else
                    zsys_warning ("(agent-smtp): smtp/encryption has unknown value, got %s, expected (NONE|TLS|STARTTLS)", encryption);

                if (streq (s_get (config, "smtp/use_auth", "false"), "true")) {
                    if (s_get (config, "smtp/user", NULL)) {
                        smtp.username (s_get (config, "smtp/user", NULL));
                    }
                    if (s_get (config, "smtp/password", NULL)) {
                        smtp.password (s_get (config, "smtp/password", NULL));
                    }
                }

                if (s_get (config, "smtp/from", NULL)) {
                    smtp.from (s_get (config, "smtp/from", NULL));
                }

                // turn on verify_ca only if smtp/verify_ca is true
                smtp.verify_ca (streq (zconfig_get (config, "smtp/verify_ca", "false"), "true"));

                // malamute
                if (zconfig_get (config, "malamute/verbose", NULL)) {
                    const char* foo = zconfig_get (config, "malamute/verbose", "false");
                    bool mlm_verbose = foo[0] == '1' ? true : false;
                    mlm_client_set_verbose (client, mlm_verbose);
                }
                if (!client_connected) {
                    if (   zconfig_get (config, "malamute/endpoint", NULL)
                        && zconfig_get (config, "malamute/address", NULL)) {

                        zstr_free (&endpoint);
                        endpoint = strdup (zconfig_get (config, "malamute/endpoint", NULL));
                        zstr_free (&name);
                        name = strdup (zconfig_get (config, "malamute/address", "fty-email"));
                        if (sendmail_only) {
                            char *oldname = name;
                            name = zsys_sprintf ("%s-sendmail-only", oldname);
                            zstr_free (&oldname);
                        }
                        uint32_t timeout = 1000;
                        sscanf ("%" SCNu32, zconfig_get (config, "malamute/timeout", "1000"), &timeout);

                        zsys_debug1 ("%s: mlm_client_connect (%s, %" PRIu32 ", %s)", name, endpoint, timeout, name);
                        int r = mlm_client_connect (client, endpoint, timeout, name);
                        if (r == -1)
                            zsys_error ("%s: mlm_client_connect (%s, %" PRIu32 ", %s) = %d FAILED", name, endpoint, timeout, name, r);
                        else
                            client_connected = true;
                    }
                    else
                        zsys_warning ("(agent-smtp): malamute/endpoint or malamute/address not in configuration, NOT connected to the broker!");
                }

                // skip if sendmail_only
                if (!sendmail_only)
                {
                    if (zconfig_locate (config, "malamute/consumers"))
                    {
                        if (mlm_client_connected (client))
                        {
                            zconfig_t *consumers = zconfig_locate (config, "malamute/consumers");
                            for (zconfig_t *child = zconfig_child (consumers);
                                 child != NULL;
                                 child = zconfig_next (child))
                            {
                                const char* stream = zconfig_name (child);
                                const char* pattern = zconfig_value (child);
                                zsys_debug1 ("%s:\tstream/pattern=%s/%s", name, stream, pattern);

                                // check if we're already connected to not let replay log to explode :)
                                if (streams.count (std::make_tuple (stream, pattern)) == 1)
                                    continue;

                                int r = mlm_client_set_consumer (client, stream, pattern);
                                if (r == -1)
                                    zsys_warning ("%s:\tcannot subscribe on %s/%s", name, stream, pattern);
                                else
                                    streams.insert (std::make_tuple (stream, pattern));
                            }
                        }
                        else
                            zsys_warning ("(agent-smtp): client is not connected to broker, can't subscribe to the stream!");
                    }
                }

                if (zconfig_get (config, "malamute/producer", NULL)) {
                    if (!mlm_client_connected (client))
                        zsys_warning ("(agent-smtp): client is not connected to broker, can't publish on the stream!");
                    else
                    if (!producer) {
                        const char* stream = zconfig_get (config, "malamute/producer", NULL);
                        int r = mlm_client_set_producer (
                                client,
                                stream);
                        if (r == -1)
                            zsys_warning ("%s:\tcannot publish on %s", name, stream);
                        else
                            producer = true;
                    }
                }

                zconfig_destroy (&config);
                zstr_free (&config_file);
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
                        mlm_client_sendtox (test_client, test_reader_name, "btest", data.c_str (), NULL);
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

        zsys_debug1 ("%s:\twhich == mlm_client", name);
        zmsg_t *zmessage = mlm_client_recv (client);
        if ( zmessage == NULL ) {
            zsys_debug1 ("%s:\tzmessage is NULL", name);
            continue;
        }
        std::string topic = mlm_client_subject(client);
        zsys_debug1("%s:\tsubject='%s'", name, topic.c_str());

        if (streq (mlm_client_command (client), "MAILBOX DELIVER")) {

            zsys_debug1 ("%s:\tMAILBOX DELIVER, subject=%s", name, mlm_client_subject (client));

            char *uuid = zmsg_popstr (zmessage);
            if (!uuid) {
                zsys_error ("UUID frame is missing from zmessage, ignoring");
                zmsg_destroy (&zmessage);
                continue;
            }

            zmsg_t *reply = zmsg_new ();
            zmsg_addstr (reply, uuid);
            zstr_free (&uuid);

            if (topic == "SENDMAIL") {
                bool sent_ok = false;
                try {
                    if (zmsg_size (zmessage) == 1) {
                        char *body = zmsg_popstr (zmessage);
                        zsys_debug1 ("%s:\tsmtp.sendmail (%s)", name, body);
                        smtp.sendmail (body);
                        zstr_free (&body);
                    }
                    else {
                        if (verbose)
                            zmsg_print (zmessage);
                        auto mail = smtp.msg2email (&zmessage);
                        if (verbose)
                            zsys_debug (mail.c_str ());
                        smtp.sendmail (mail);
                    }
                    zmsg_addstr (reply, "0");
                    zmsg_addstr (reply, "OK");
                    sent_ok = true;
                }
                catch (const std::runtime_error &re) {
                    zsys_debug1 ("%s:\tgot std::runtime_error, e.what ()=%s", name, re.what ());
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
            else if (topic == "SENDMAIL_ALERT") {
                fty_proto_t *alert = fty_proto_decode (&zmessage);
                try {
                    s_notify (smtp, elements, alert);
                    zmsg_addstr (reply, "OK");
                }
                catch (const std::runtime_error &re) {
                    zsys_error ("Sending or e-mail/SMS alert failed : %s", re.what ());
                    zmsg_addstr (reply, "ERROR");
                    zmsg_addstr (reply, re.what ());
                }
                int r = mlm_client_sendto (
                        client,
                        mlm_client_sender (client),
                        "SENDMAIL_ALERT",
                        NULL,
                        1000,
                        &reply);
                if (r == -1)
                    zsys_error ("Can't send a reply for SENDMAIL to %s", mlm_client_sender (client));
            }
            else
                zsys_warning ("%s:\tUnknown subject %s", name, topic.c_str ());

            zmsg_destroy (&reply);
            zmsg_destroy (&zmessage);
            continue;
        }

        // There are inputs
        //  - an asset config message
        //  - an SMTP settings TODO
        if (is_fty_proto (zmessage)) {
            fty_proto_t *bmessage = fty_proto_decode (&zmessage);
            if (!bmessage) {
                zsys_error ("cannot decode fty_proto message, ignore it");
                continue;
            }
            else if (fty_proto_id (bmessage) == FTY_PROTO_ASSET)  {
                onAssetReceive (&bmessage, elements, sms_gateway, verbose);
            }
            else {
                zsys_error ("it is not an alert message, ignore it");
            }
            fty_proto_destroy (&bmessage);
        }
        zmsg_destroy (&zmessage);
    }

    // save info to persistence before I die
    if (!sendmail_only)
        elements.save();
    zstr_free (&name);
    zstr_free (&endpoint);
    zstr_free (&test_reader_name);
    zstr_free (&sms_gateway);
    zpoller_destroy (&poller);
    mlm_client_destroy (&client);
    mlm_client_destroy (&test_client);
    zclock_sleep(1000);
}


//  -------------------------------------------------------------------------
//

/*
 * \brief helper function, that creates an smtp server as it would be created
 *      in the real environment
 *
 *  \param[in] verbose - if function should produce debug information or not
 *  \param[in] endpoint - endpoint of malamute where to connect
 *  \param[in] assets_file - an absolute path to the "asset" state file
 *  \param[in] agent_name - what agent name should be registred in malamute
 *  \param[in] clear_assets - do we want to clear "asset" state file before
 *                      smtp agent will start
 *                      smtp agent will start
 *  \return smtp agent actor
 */
static zactor_t* create_test_smtp_server (
    bool verbose,
    const char *endpoint,
    const char *assets_file,
    const char *agent_name,
    bool clear_assets
    )
{
    // Note: mkstemp fixes up contents of this array
    char temp_config_file[PATH_MAX] = {"/tmp/.fty-email-tempcfg.XXXXXX"};
    int config_fd =  mkstemp(temp_config_file);
    if (config_fd < 0) {
        zsys_error("create_smtp_server(): could not create a temporary config file");
// FIXME : is NULL a valid return here? or should we assert() to die on FS errors?
        return NULL;
    }
    close(config_fd);

    if ( clear_assets )
        std::remove (assets_file);
    zactor_t *smtp_server = zactor_new (fty_email_server, NULL);
    assert ( smtp_server != NULL );
    zconfig_t *config = zconfig_new ("root", NULL);
    zconfig_put (config, "server/assets", assets_file);
    zconfig_put (config, "malamute/endpoint", endpoint);
    zconfig_put (config, "malamute/address", agent_name);
    zconfig_put (config, "malamute/consumers/ASSETS", ".*");
    zconfig_save (config, temp_config_file);
    zconfig_destroy (&config);
    if ( verbose )
        zstr_send (smtp_server, "VERBOSE");
    zstr_sendx (smtp_server, "LOAD", temp_config_file, NULL);
// FIXME : perhaps better ack via protocol that the actor is ready to work?
    zclock_sleep (1500);
// FIXME : should we remove this file? Did not do so in other code locations...
    unlink(temp_config_file);
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
    const char *extname,
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
    if ( extname )
        zhash_insert (ext, "extname", (void *)extname);

    zmsg_t *msg = fty_proto_encode_asset (aux, asset_name, operation, ext);
    assert (msg);
    int rv = mlm_client_send(producer, asset_name, &msg);
    assert ( rv == 0 );
    if ( verbose )
        zsys_info ("asset message was sent");
    zhash_destroy (&aux);
    zhash_destroy (&ext);
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

    // Note: If your selftest reads SCMed fixture data, please keep it in
    // src/selftest-ro; if your test creates filesystem objects, please
    // do so under src/selftest-rw. They are defined below along with a
    // usecase for the variables (assert) to make compilers happy.
    const char *SELFTEST_DIR_RO = "src/selftest-ro";
    const char *SELFTEST_DIR_RW = "src/selftest-rw";
    assert (SELFTEST_DIR_RO);
    assert (SELFTEST_DIR_RW);
    // Uncomment these to use C++ strings in C++ selftest code:
    // std::string str_SELFTEST_DIR_RO = std::string(SELFTEST_DIR_RO);
    // std::string str_SELFTEST_DIR_RW = std::string(SELFTEST_DIR_RW);
    // assert ( (str_SELFTEST_DIR_RO != "") );
    // assert ( (str_SELFTEST_DIR_RW != "") );
    // NOTE that for "char*" context you need (str_SELFTEST_DIR_RO + "/myfilename").c_str()

    // we want new smtp server with empty states
    /*char *alerts_file = zsys_sprintf ("%s/test10_alerts.xtx", SELFTEST_DIR_RW);
    assert (alerts_file!=NULL);*/
    char *assets_file = zsys_sprintf ("%s/test10_assets.xtx", SELFTEST_DIR_RW);
    assert (assets_file!=NULL);

    ElementList elements; // element list to load
    Element element; // one particular element to check

    zactor_t *smtp_server = create_test_smtp_server
        (verbose, endpoint, assets_file, "smtp-10", true);

    // test10-1 (create NOT known asset)
    s_send_asset_message (verbose, asset_producer, "1", "scenario10.email@eaton.com",
                          "assetik_10_1", "scenario10 Support Eaton", "create", "ASSET_10_1",
                          "somephone");
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
                          "assetik_10_1", "scenario10 Support Eaton", "update", "ASSET_10_1");
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
    s_send_asset_message (verbose, asset_producer, NULL, NULL, "assetik_10_1",
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
    s_send_asset_message (verbose, asset_producer, "1", "scenario10.email@eaton.com", "assetik_10_1",
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
    s_send_asset_message (verbose, asset_producer, "2", "scenario10.email2@eaton.com", "assetik_10_1",
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
    s_send_asset_message (verbose, asset_producer, "3", "scenario10.email@eaton.com","assetik_10_1",
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
    s_send_asset_message (verbose, asset_producer, "3", "scenario103.email@eaton.com","assetik_10_1",
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
    s_send_asset_message (verbose, asset_producer, NULL, NULL,"assetik_10_1",
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
    s_send_asset_message (verbose, asset_producer, "5", "scenario105.email@eaton.com","assetik_10_1",
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
//    zstr_free (&alerts_file);
    zstr_free (&assets_file);
}

//  Self test of this class
void
fty_email_server_test (bool verbose)
{
    // Note: If your selftest reads SCMed fixture data, please keep it in
    // src/selftest-ro; if your test creates filesystem objects, please
    // do so under src/selftest-rw. They are defined below along with a
    // usecase for the variables (assert) to make compilers happy.
    const char *SELFTEST_DIR_RO = "src/selftest-ro";
    const char *SELFTEST_DIR_RW = "src/selftest-rw";
    assert (SELFTEST_DIR_RO);
    assert (SELFTEST_DIR_RW);
    // Uncomment these to use C++ strings in C++ selftest code:
    // std::string str_SELFTEST_DIR_RO = std::string(SELFTEST_DIR_RO);
    // std::string str_SELFTEST_DIR_RW = std::string(SELFTEST_DIR_RW);
    // assert ( (str_SELFTEST_DIR_RO != "") );
    // assert ( (str_SELFTEST_DIR_RW != "") );
    // NOTE that for "char*" context you need (str_SELFTEST_DIR_RO + "/myfilename").c_str()

    char *assets_file = zsys_sprintf ("%s/kkk_assets.xtx", SELFTEST_DIR_RW);
    assert (assets_file!=NULL);
    std::remove (assets_file);

    char *pidfile = zsys_sprintf ("%s/btest.pid", SELFTEST_DIR_RW);
    assert (pidfile!=NULL);

    char *smtpcfg_file = zsys_sprintf ("%s/smtp.cfg", SELFTEST_DIR_RW);
    assert (smtpcfg_file!=NULL);

    printf (" * fty_email_server: ");
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

    {
    zhash_t *headers = zhash_new ();
    zhash_update (headers, "Foo", (void*) "bar");
    char *file1_name = zsys_sprintf ("%s/file1", SELFTEST_DIR_RW);
    assert (file1_name!=NULL);
    char *file2_name = zsys_sprintf ("%s/file2.txt", SELFTEST_DIR_RW);
    assert (file2_name!=NULL);
    zmsg_t *email_msg = fty_email_encode (
            "UUID",
            "TO",
            "SUBJECT",
            headers,
            "BODY",
            file1_name,
            file2_name,
            NULL);
    assert (email_msg);
    assert (zmsg_size (email_msg) == 7);
    zhash_destroy (&headers);

    char *uuid = zmsg_popstr (email_msg);
    char *to = zmsg_popstr (email_msg);
    char *csubject = zmsg_popstr (email_msg);
    char *body = zmsg_popstr (email_msg);

    assert (streq (uuid, "UUID"));
    assert (streq (to, "TO"));
    assert (streq (csubject, "SUBJECT"));
    assert (streq (body, "BODY"));

    zstr_free (&uuid);
    zstr_free (&to);
    zstr_free (&csubject);
    zstr_free (&body);

    zframe_t *frame = zmsg_pop (email_msg);
    assert (frame);
    headers = zhash_unpack (frame);
    zframe_destroy (&frame);

    assert (streq ((char*)zhash_lookup (headers, "Foo"), "bar"));
    zhash_destroy (&headers);

    char *file1 = zmsg_popstr (email_msg);
    char *file2 = zmsg_popstr (email_msg);
    char *file3 = zmsg_popstr (email_msg);

    zsys_debug("Got file1='%s'\nExpected ='%s'", file1, file1_name );
    zsys_debug("Got file2='%s'\nExpected ='%s'", file2, file2_name );

    assert (streq (file1, file1_name ));
    assert (streq (file2, file2_name ));
    assert (!file3);

    zstr_free (&file1);
    zstr_free (&file2);
    zstr_free (&file1_name);
    zstr_free (&file2_name);
    zmsg_destroy (&email_msg);

    }

    static const char* endpoint = "inproc://fty-smtp-server-test";

    // malamute broker
    zactor_t *server = zactor_new (mlm_server, (void*) "Malamute");
    assert ( server != NULL );
    zstr_sendx (server, "BIND", endpoint, NULL);
    if ( verbose )
        zsys_info ("malamute started");

    // similar to create_test_smtp_server
    zactor_t *smtp_server = zactor_new (fty_email_server, NULL);
    assert ( smtp_server != NULL );

    zconfig_t *config = zconfig_new ("root", NULL);
    zconfig_put (config, "server/assets", assets_file);
    zconfig_put (config, "malamute/endpoint", endpoint);
    zconfig_put (config, "malamute/address", "agent-smtp");
    zconfig_put (config, "malamute/consumers/ASSETS", ".*");
    zconfig_save (config, smtpcfg_file);
    zconfig_destroy (&config);

    if (verbose)
        zstr_send (smtp_server, "VERBOSE");
    zstr_sendx (smtp_server, "LOAD", smtpcfg_file, NULL);
    zstr_sendx (smtp_server, "_MSMTP_TEST", "btest-reader", NULL);
    if ( verbose )
        zsys_info ("smtp server started");

    mlm_client_t *alert_producer = mlm_client_new ();
    int rv = mlm_client_connect (alert_producer, endpoint, 1000, "alert_producer");
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
    zmsg_t *msg = fty_proto_encode_asset (aux, asset_name, "create", ext);
    assert (msg);
    mlm_client_send (asset_producer, "Asset message1", &msg);
    zhash_destroy (&aux);
    zhash_destroy (&ext);
    if (verbose)
        zsys_info ("asset message was sent");
    // Ensure, that malamute will deliver ASSET message before ALERT message
    zclock_sleep (1000);

    //      2. send alert message
    //TODO: replace with SENDMAIL_ALERT
    /*msg = fty_proto_encode_alert (NULL, zclock_time ()/1000, 600, "NY_RULE", asset_name, \
                                  "ACTIVE","CRITICAL","ASDFKLHJH", "EMAIL");
    assert (msg);
    std::string atopic = "NY_RULE/CRITICAL@" + std::string (asset_name);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    if (verbose)
        zsys_info ("alert message was sent");*/

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

    // expected string without date
    std::string expectedBody = "From:bios@eaton.com\nTo: scenario1.email@eaton.com\nSubject: CRITICAL alert on ASSET1 from the rule ny_rule is active!\n\n"
    "In the system an alert was detected.\nSource rule: ny_rule\nAsset: ASSET1\nAlert priority: P1\nAlert severity: CRITICAL\n"
    "Alert description: ASDFKLHJH\nAlert state: ACTIVE\n";
    expectedBody.erase(remove_if(expectedBody.begin(), expectedBody.end(), isspace), expectedBody.end());

    if (verbose) {
        zsys_debug ("expectedBody =\n%s", expectedBody.c_str ());
        zsys_debug ("\n");
        zsys_debug ("newBody =\n%s", newBody.c_str ());
    }
    //FIXME: email body is created by cxxtools::MimeMultipart class - do we need to test it?
    //assert ( expectedBody.compare(newBody) == 0 );

    // scenario 2: send an alert on the unknown asset
    //      1. DO NOT send asset info
    //const char *asset_name1 = "ASSET2";

    //      2. send alert message
    //TODO: replace with SENDMAIL_ALERT
    /*msg = fty_proto_encode_alert (NULL, time (NULL), 600, "NY_RULE", asset_name1, \
                                  "ACTIVE","CRITICAL","ASDFKLHJH", "EMAIL");
    assert (msg);
    std::string atopic1 = "NY_RULE/CRITICAL@" + std::string (asset_name1);
    mlm_client_send (alert_producer, atopic1.c_str(), &msg);
    if (verbose)
        zsys_info ("alert message was sent");*/

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
    msg = fty_proto_encode_asset (aux, asset_name3, "update", ext);
    assert (msg);
    mlm_client_send (asset_producer, "Asset message3", &msg);
    zhash_destroy (&aux);
    zhash_destroy (&ext);
    if (verbose)
        zsys_info ("asset message was sent");

    //      2. send alert message
    //TODO: replace with SENDMAIL_ALERT
    /*msg = fty_proto_encode_alert (NULL, time (NULL), 600, "NY_RULE", asset_name3, \
                                  "ACTIVE","CRITICAL","ASDFKLHJH", "EMAIL");
    assert (msg);
    std::string atopic3 = "NY_RULE/CRITICAL@" + std::string (asset_name3);
    mlm_client_send (alert_producer, atopic3.c_str(), &msg);
    if (verbose)
        zsys_info ("alert message was sent");*/

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
    //TODO: replace with SENDMAIL_ALERT
    /*atopic = "Scenario4/CRITICAL@" + std::string (asset_name);
    msg = fty_proto_encode_alert (NULL, time (NULL), 600, "Scenario4", asset_name, \
                                  "ACTIVE","CRITICAL","ASDFKLHJH", "EMAIL");
    assert (msg);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    if (verbose)
        zsys_info ("alert message was sent");*/

    //      2. read the email generated for alert
    msg = mlm_client_recv (btest_reader);
    assert (msg);
    if ( verbose ) {
        zsys_debug ("parameters for the email:");
        zmsg_print (msg);
    }
    zmsg_destroy (&msg);

    //      4. send an alert on the already known asset
    //TODO: replace with SENDMAIL_ALERT
    /*msg = fty_proto_encode_alert (NULL, time (NULL), 600, "Scenario4", asset_name, \
                                  "ACTIVE","CRITICAL","ASDFKLHJH", "EMAIL");
    assert (msg);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    if (verbose)
        zsys_info ("alert message was sent");*/

    //      5. email should not be sent (it doesn't satisfy the schedule
    poller = zpoller_new (mlm_client_msgpipe(btest_reader), NULL);
    which = zpoller_wait (poller, 1000);
    assert ( which == NULL );
    if ( verbose ) {
        zsys_debug ("No email was sent: SUCCESS");
    }
    zpoller_destroy (&poller);

    // scenario 5: alert without action "EMAIL"
    //      1. send alert message
    //TODO: replace with SENDMAIL_ALERT
    /*msg = fty_proto_encode_alert (NULL, time (NULL), 600, "NY_RULE", asset_name3, \
                                  "ACTIVE","CRITICAL","ASDFKLHJH", "SMS");
    assert (msg);
    mlm_client_send (alert_producer, atopic3.c_str(), &msg);
    if (verbose)
        zsys_info ("alert message was sent");*/

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
    msg = fty_proto_encode_asset (aux, asset_name6, "create", ext);
    assert (msg);
    rv = mlm_client_send (asset_producer, "Asset message6", &msg);
    assert ( rv != -1 );
    // Ensure, that malamute will deliver ASSET message before ALERT message
    zclock_sleep (1000);

    //      2. send alert message
    //TODO: replace with SENDMAIL_ALERT
    /*msg = fty_proto_encode_alert (NULL, time (NULL), 600, rule_name6, asset_name6, \
                                  "ACTIVE","CRITICAL","ASDFKLHJH", "EMAIL");
    assert (msg);
    rv = mlm_client_send (alert_producer, alert_topic6.c_str(), &msg);
    assert ( rv != -1 );*/

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
    msg = fty_proto_encode_asset (aux, asset_name6, "update", ext);
    assert (msg);
    rv = mlm_client_send (asset_producer, "Asset message6", &msg);
    assert ( rv != -1 );
    zhash_destroy (&aux);
    zhash_destroy (&ext);
    // Ensure, that malamute will deliver ASSET message before ALERT message
    zclock_sleep (1000);

    //      5. send alert message again
    //TODO: replace with SENDMAIL_ALERT
    /*msg = fty_proto_encode_alert (NULL, time (NULL), 600, rule_name6, asset_name6, \
                                  "ACTIVE","CRITICAL","ASDFKLHJH", "EMAIL");
    assert (msg);
    rv = mlm_client_send (alert_producer, alert_topic6.c_str(), &msg);
    assert ( rv != -1 );*/

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

    // expected string without date
    expectedBody = "From:bios@eaton.com\nTo: scenario6.email@eaton.com\nSubject: CRITICAL alert on asset_6 from the rule rule_name_6 is active!\n\n"
    "In the system an alert was detected.\nSource rule: rule_name_6\nAsset: asset_6\nAlert priority: P1\nAlert severity: CRITICAL\n"
    "Alert description: ASDFKLHJH\nAlert state: ACTIVE\n";
    expectedBody.erase(remove_if(expectedBody.begin(), expectedBody.end(), isspace), expectedBody.end());
    //FIXME: use cxxtools::MimeMultipart, rewrite
    //assert ( expectedBody.compare(newBody) == 0 );

    // intentionally left formatting intact, so git blame will refer to original author ;-)
    if (verbose) {
    zsys_debug (" scenario 7 ===============================================");
    // scenario 7:
    //      1. send an alert on the already known asset
    //TODO: replace with SENDMAIL_ALERT
    /*atopic = "Scenario7/CRITICAL@" + std::string (asset_name);
    msg = fty_proto_encode_alert (NULL, time (NULL), 600, "Scenario7", asset_name, \
                                  "ACTIVE","CRITICAL","ASDFKLHJH", "EMAIL");
    assert (msg);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    if (verbose)
        zsys_info ("alert message was sent");*/

    //      2. read the email generated for alert
    msg = mlm_client_recv (btest_reader);
    assert (msg);
    if ( verbose ) {
        zsys_debug ("parameters for the email:");
        zmsg_print (msg);
    }
    zmsg_destroy (&msg);

    //      4. send an alert on the already known asset
    //TODO: replace with SENDMAIL_ALERT
    /*msg = fty_proto_encode_alert (NULL, time (NULL), 600, "Scenario4", asset_name, \
                                  "ACK-SILENCE","CRITICAL","ASDFKLHJH", "EMAIL");
    assert (msg);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    if (verbose)
        zsys_info ("alert message was sent");*/

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
    //TODO: replace with SENDMAIL_ALERT
    /*msg = fty_proto_encode_alert (NULL, time (NULL), 600, "Scenario4", asset_name, \
                                  "ACK-SILENCE","CRITICAL","ASDFKLHJH", "EMAIL");
    assert (msg);
    mlm_client_send (alert_producer, atopic.c_str(), &msg);
    if (verbose)
        zsys_info ("alert message was sent");*/

    //      8. email should not be sent (it is in the state, where alerts are not being sent)
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
    msg = fty_proto_encode_asset (aux, asset_name8, "create", ext);
    assert (msg);
    rv = mlm_client_send (asset_producer, "Asset message8", &msg);
    assert ( rv != -1 );
    // Ensure, that malamute will deliver ASSET message before ALERT message
    zclock_sleep (1000);

    //      2. send alert message
    //TODO: replace with SENDMAIL_ALERT
    /*msg = fty_proto_encode_alert (NULL, time (NULL), 600, rule_name8, asset_name8, \
                                  "ACTIVE","WARNING","Default load in ups ROZ.UPS36 is high", "EMAIL/SMS");
    assert (msg);
    rv = mlm_client_send (alert_producer, alert_topic6.c_str(), &msg);
    assert ( rv != -1 );*/

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
    msg = fty_proto_encode_asset (aux, asset_name8, "update", ext);
    assert (msg);
    rv = mlm_client_send (asset_producer, "Asset message8", &msg);
    assert ( rv != -1 );
    zhash_destroy (&aux);
    zhash_destroy (&ext);
    // Ensure, that malamute will deliver ASSET message before ALERT message
    zclock_sleep (1000);

    //      5. send alert message again second
    //TODO: replace with SENDMAIL_ALERT
    /*msg = fty_proto_encode_alert (NULL, time (NULL), 600, rule_name8, asset_name8, \
                                  "ACTIVE","WARNING","Default load in ups ROZ.UPS36 is high", "EMAIL/SMS");
    assert (msg);
    rv = mlm_client_send (alert_producer, alert_topic8.c_str(), &msg);
    assert ( rv != -1 );*/

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
    //TODO: replace with SENDMAIL_ALERT
    /*msg = fty_proto_encode_alert (NULL, time(NULL), 600, rule_name8, asset_name8, \
                                  "ACTIVE","WARNING","Default load in ups ROZ.UPS36 is high", "EMAIL");
    assert (msg);
    rv = mlm_client_send (alert_producer, alert_topic8.c_str(), &msg);
    assert ( rv != -1 );*/

    //      9. Email SHOULD NOT be generated
    poller = zpoller_new (mlm_client_msgpipe (btest_reader), NULL);
    which = zpoller_wait (poller, 1000);
    assert ( which == NULL );
    if ( verbose ) {
        zsys_debug ("Email was NOT sent: SUCCESS");
    }
    zpoller_destroy (&poller);

    //test SENDMAIL
    rv = mlm_client_sendtox (alert_producer, "agent-smtp", "SENDMAIL", "UUID", "foo@bar", "Subject", "body", NULL);
    assert (rv != -1);
    msg = mlm_client_recv (alert_producer);
    assert (streq (mlm_client_subject (alert_producer), "SENDMAIL-OK"));
    assert (zmsg_size (msg) == 3);

    char *uuid = zmsg_popstr (msg);
    assert (streq (uuid, "UUID"));
    zstr_free (&uuid);

    char *code = zmsg_popstr (msg);
    assert (streq (code, "0"));
    zstr_free (&code);

    char *reason = zmsg_popstr (msg);
    assert (streq (reason, "OK"));
    zstr_free (&reason);

    zmsg_destroy (&msg);

    //  this fixes the reported memcheck error
    msg = mlm_client_recv (btest_reader);
    if (verbose)
        zmsg_print (msg);
    zmsg_destroy (&msg);

    test10 (verbose, endpoint, server, asset_producer);

    // clean up after the test


    // smtp server send mail only
    zactor_t *send_mail_only_server = zactor_new (fty_email_server, (void*) "sendmail-only");
    assert ( send_mail_only_server != NULL );

    if (verbose)
    {
        zsys_info ("smtp-sedmail-only server started");
        zstr_send (send_mail_only_server, "VERBOSE");
    }

    zactor_destroy(&send_mail_only_server);
    zactor_destroy (&smtp_server);
    mlm_client_destroy (&btest_reader);
    mlm_client_destroy (&asset_producer);
    mlm_client_destroy (&alert_producer);
    zactor_destroy (&server);
    zstr_free (&assets_file);
    zstr_free (&pidfile);
    zstr_free (&smtpcfg_file);

    printf ("OK\n");
}
