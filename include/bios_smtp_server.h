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

#ifndef BIOS_SMTP_SERVER_H_INCLUDED
#define BIOS_SMTP_SERVER_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

//  @interface

//  Main actor sending emails (and sms2email)
//  Configuration format
//  ====================
//
//  server
//      verbose             1 turns verbose mode on, 0 off
//      assets              path to state file for assets
//      alerts              path to state file for alerts
//  smtp
//      server              address of smtp server
//      port                port number
//      user                name of user for login
//      password            password of user
//      from                From: header of email
//      encryption          encryption, can be (none|tls|starttls)
//      msmtppath           path to msmtp command
//      smsgateway          email to sms gateway
//      verify_ca           1 turns on CA verification, 0 off
//  malamute
//      verbose             1 setup verbose mode of mlm_client, 0 turn it off
//      endpoint            malamute endpoint address
//      address             mailbox address of agent-smtp
//      consumers
//          ALERTS  .*      consume all messages on ALERTS stream
//          ASSETS  .*      consume all messages on ASSETS stream
//
//  Actor commands
//  ==============
//
//  LOAD    path            load and apply configuration from zpl file
//                          see Configuration format section
//
//  Malamute protocol (mailbox agent-smtp)
//  ======================================
//
//  REQ: subject=SENDMAIL [$uuid|$to|$subject|$body] or [$uuid|$body]
//      sends emails via configured environment to address $to, with subject $subject and body $body
//      alternativelly the whole email body can be passed
//  REP: subject=SENDMAIL-OK [$uuid|0|OK]
//      if email was sent
//  REP: subject=SENDMAIL-ERR [$uuid|$error code|$error message]
//      if email wasn't sent, or there was improper number of arguments
//      error message comes from msmtp stderr and is NOT normalized!
AGENT_SMTP_EXPORT void
   bios_smtp_server (zsock_t *pipe, void* args);

//  Self test of this class
AGENT_SMTP_EXPORT void
    bios_smtp_server_test (bool verbose);
//  @end

#ifdef __cplusplus
}
#endif

#endif
