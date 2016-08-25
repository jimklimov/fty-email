/*  =========================================================================
    email - Smtp

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

/*! \file   email.h
    \brief  Simple wrapper on top of msmtp to send an email
    \author Michal Vyskocil <MichalVyskocil@Eaton.com>

Example:

    shared::Smtp smtp{"mail.example.com", "joe.doe@example.com"};

    try {
        smtp.sendmail(
            "agent.smith@matrix.gov",
            "The pill taken by Neo",
            "Dear Mr. Smith,\n......");
    }
    catch (const std::runtime_error& e) {
       // here we'll handle the error
    }

*/
#ifndef EMAIL_H_INCLUDED
#define EMAIL_H_INCLUDED


#include <string>
#include <vector>

#include "subprocess.h"

/**
 * \class security
 *
 * Security of SMTP connection
 */
enum class Enctryption {
    NONE,
    TLS,
    STARTTLS
};

/**
 * \class Smtp
 *
 * \brief Simple wrapper on top of msmtp
 *
 * This class contain some basic configuration for
 * msmtp (host/from) + provide sendmail methods.
 * It *DOES NOT* perform any additional transofmation
 * like uuencode or mime. IOW garbage-in, garbage-out.
 */
class Smtp
{
    public:
        /**
         * \brief Creates SMTP instance
         *
         */
        explicit Smtp();

        /** \brief set the SMTP server address */
        void host (const std::string& host) { _host = host; };

        /** \brief set the SMTP server port. Default is 25.*/
        void port (const std::string& port) { _port = port; };

        /** \brief set the "mail from" address */
        void from (const std::string& from) { _from = from; };

        /** \brief set username for smtp authentication */
        void username (const std::string& username) { _username = username; };

        /** \brief set password for smtp authentication */
        void password (const std::string& password) { _password = password; };

        /** \brief set the encryption for SMTP communication (NONE|TLS|STARTTLS) */
        void encryption (std::string enc);
        void encryption (Enctryption enc) { _encryption = enc; };

        /**
         * \brief set alternative path for msmtp
         *
         * \param path  path to msmtp binary to be called
         *
         */
        void msmtp_path (const std::string& msmtp_path) { _msmtp = msmtp_path; };

        /**
         * \brief set sendmail testing function
         *
         * \param function to be used in sendmail instead of msmtp binary
         * This is for testing purposes of protocol only!
         */
        void sendmail_set_test_fn (std::function <void(const std::string&)> fn) {
            _has_fn = true;
            _fn = fn;
        }

        /**
         * \brief send the email
         *
         * Technically this put email to msmtp's outgoing queue
         * \param to        email header To: multiple recipient in vector
         * \param subject   email header Subject:
         * \param body      email body
         *
         * \throws std::runtime_error for msmtp invocation errors
         */
        void sendmail(
                const std::vector<std::string> &to,
                const std::string& subject,
                const std::string& body) const;

        /**
         * \brief send the email
         *
         * Technically this put email to msmtp's outgoing queue
         * \param to        email header To: single recipient
         * \param subject   email header Subject:
         * \param body      email body
         *
         * \throws std::runtime_error for msmtp invocation errors
         */
        void sendmail(
                const std::string& to,
                const std::string& subject,
                const std::string& body) const;

    protected:

        /**
         * \brief send the email
         *
         * Technically this put email to msmtp's outgoing queue
         * \param data  email DATA (To/Subject are deduced
         *              from the fields in body, so body must be properly
         *              formatted email message).
         *
         * \throws std::runtime_error for msmtp invocation errors
         */
        void sendmail(
                const std::string& data) const;

        /**
         * \brief create msmtp config file
         */
        std::string createConfigFile() const;
        /**
         * \brief delete msmtp config file
         */
        void deleteConfigFile(std::string &filename) const;

        std::string _host;
        std::string _port;
        std::string _from;
        Enctryption _encryption;
        std::string _username;
        std::string _password;
        std::string _msmtp;
        bool _has_fn;
        std::function <void(const std::string&)> _fn;
};

void email_test (bool verbose);

#endif
