# fty-email

fty-email is an agent which sends e-mail/SMS notifications about alarms.

## How to build

To build fty-email project run:

```bash
./autogen.sh
./configure
make
make check # to run self-test
```
Compilation of fty-email creates two binaries - fty-email, which is run by systemd service, and fty-sendmail, which is a CLI utility.

Distributed together with them is a shell script fty-device-scan, which scans SNMP-capable power devices and reports the result via e-mail.

## How to run

To run fty-email project:

* from within the source tree, run:

```bash
./src/fty-email
```

For the other options available, refer to the manual page of fty-email

* from an installed base, using systemd, run:

```bash
systemctl start fty-email
```

### Configuration file

To configure fty-email, a configuration file exists: fty-email.cfg

Beside from the standard configuration directives, under the server and malamute
sections, agent has the following configuration options:

* under server section:
    * alerts -  path to the state file with alerts cache (default value /var/lib/fty/fty-email/alerts)
    * assets - path to the state file with assets cache (default value /var/lib/fty/fty-email/assets)

* under smtp section:
    * server - SMTP server
    * port - port of SMTP server (default value 25)
    * user - SMTP user name
    * password - SMTP user password
    * from - From: header
    * msmtppath - path to msmtp binary
    * encryption - available values: NONE | TLS | STARTTLS (default value NONE)
    * smsgateway - SMS gateway
    * verify\_ca - whether to verify CA
    * use\_auth - whether to use username and password

* under malamute section:
    * consumers/<stream> - subscribe fty-email to specified streams and use regular expression filtering on them.
        Leave at default values unless you KNOW what you are doing.
    * producer - set fty-email to publish on specified stream
    * timeout - set timeout for connecting to Malamute broker (default value 1s)

Values read from configuration file can be overwritten by setting the following environment variables:

* BIOS\_LOG\_LEVEL for server/verbose
* BIOS\_SMTP\_SERVER for smtp/server
* BIOS\_SMTP\_PORT for smtp/port
* BIOS\_SMTP\_USER for smtp/user
* BIOS\_SMTP\_PASSWD for smtp/password
* BIOS\_SMTP\_FROM for smtp/from
* \_MSMTP\_PATH for smtp/msmtp
* BIOS\_SMTP\_ENCRYPT for smtp/encryption
* BIOS\_SMTP\_SMS\_GATEWAY for smtp/smsgateway
* BIOS\_SMTP\_VERIFY\_CA for smtp/verify\_ca

## Architecture

### Overview

fty-email is composed of 1 actor and 1 timer.

Actor is a server actor: handles e-mail configuration, notification via e-mail/SMS and requests to send e-mail in general.

This actor can be run in full, or in sendmail-only mode (when it doesn't connect to the Malamute broker).

Timer is a notification timer - runs every second and checks the alerts cache to see whether we have any alerts for which we need to send e-mail/SMS notification.

## Protocols

### Published metrics

In default configuration, agent doesn't publish any metrics.

### Published alerts

In default configuration, agent doesn't publish any alerts.

### Sending e-mails

Sending of e-mails is handled by class email, which implements a wrapper for msmtp binary.

NB: configuration is loaded only once at the start of the server actor.

### Mailbox requests

It is possible to request the fty-email agent for:

* sending e-mail with default headers

* sending e-mail with user-specified headers

#### Sending e-mail with default headers

The USER peer sends the following messages using MAILBOX SEND to
FTY-EMAIL-AGENT ("fty-email") peer:

* SENDMAIL/correlation\-id/body - send e-mail with default headers with the body 'body'

where
* '/' indicates a multipart string message
* 'correlation\-id' is a zuuid identifier provided by the caller
* 'body' MUST be valid body of e-mail
* subject of the message MUST be "SENDMAIL".

The FTY-SENSOR-GPIO-AGENT peer MUST respond with one of the messages back to USER
peer using MAILBOX SEND.

* correlation\-id/0/OK
* correlation\-id/error\-code/reason

where
* '/' indicates a multipart frame message
* 'correlation\-id' is a zuuid identifier provided by the caller
* 'error\-code' is error code from msmtp
* 'reason' is string detailing reason for error (just what() for the thrown runtime error)
* subject of the message must be SENDMAIL-OK for OK message and SENDMAIL-ERROR for error message

#### Sending e-mail with user-specified headers

The USER peer sends the following messages using MAILBOX SEND to
FTY-EMAIL-AGENT ("fty-email") peer:

* SENDMAIL/correlation\-id/to/subject/body/header\-1/.../header\-n/path\-1/.../path\-m - send this e-mail

where
* '/' indicates a multipart string message
* 'correlation\-id' is a zuuid identifier provided by the caller
* 'to' MUST be valid To: header
* 'subject' MUST be valiud Subject: header
* 'body' MUST be valid body of e-mail
* 'header-1',...,'header-n' MAY be other headers
* 'path-1',...,'path-m' MAY be present and MUST be, if present, paths to text OR binary files
* subject of the message MUST be "SENDMAIL".

The FTY-SENSOR-GPIO-AGENT peer MUST respond with one of the messages back to USER
peer using MAILBOX SEND.

* correlation\-id/0/OK
* correlation\-id/error\-code/reason

where
* '/' indicates a multipart frame message
* 'correlation\-id' is a zuuid identifier provided by the caller
* 'error\-code' is error code from msmtp
* 'reason' is string detailing reason for error (just what() for the thrown runtime error)
* subject of the message must be SENDMAIL-OK for OK message and SENDMAIL-ERROR for error message

### Stream subscriptions

In default configuration, agent is subscribed to streams ALERTS and ASSETS.

When it receives an alert:

* it updates its local alert cache and alert state file
* it checks whether we are supposed to send a notification for this alert

When it receives an asset:

* it extracts contact information (contact name, e-mail, phone number)
* it extracts information about priority
* it updates its local asset cache and asset state file
