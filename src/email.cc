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

/*
@header
    email - Smtp
@discuss
@end
*/

#include "agent_smtp_classes.h"

//  Structure of our class
#include <sstream>
#include <ctime>
#include <stdio.h>

Smtp::Smtp():
    _host {},
    _port { "25" },
    _from { "bios@eaton.com" },
    _encryption { Enctryption::NONE },
    _username {},
    _password {},
    _msmtp { "/usr/bin/msmtp" },
    _has_fn {false},
    _verify_ca {false}
{

}

std::string Smtp::createConfigFile() const
{
    char filename[] = "/tmp/bios-msmtp-XXXXXX.cfg";
    int handle = mkstemps(filename,4);
    std::string line;

    line = "defaults\n";
    const std::string verify_ca = _verify_ca ? "on" : "off";

    switch (_encryption) {
    case Enctryption::NONE:
        line += "tls off\n"
                "tls_starttls off\n";
        break;
    case Enctryption::TLS:
        line += "tls on\n"
                "tls_certcheck " + verify_ca + "\n";
        break;
    case Enctryption::STARTTLS:
        // TODO: check if this is correct!
        line += "tls off\n"
                "tls_certcheck " + verify_ca + "\n"
                "tls_starttls on\n";
        break;
    }
    if (_username.empty()) {
        line += "auth off\n";
    } else {
        line += "auth on\n"
            "user " + _username + "\n"
            "password " + _password + "\n";
    }

    line += "account default\n";
    line += "host " + _host +"\n";
    line += "from " + _from + "\n";
    ssize_t r = write (handle,  line.c_str(), line.size());
    if (r > 0 && (size_t) r != line.size ())
        zsys_error ("write to %s was truncated, expected %zu, written %zd", filename, line.size(), r);
    if (r == -1)
        zsys_error ("write to %s failed: %s", filename, strerror (errno));
    close (handle);
    return std::string(filename);
}

void Smtp::deleteConfigFile(std::string &filename) const
{
    unlink (filename.c_str());
}

void Smtp::encryption(std::string enc)
{
    if( strcasecmp ("starttls", enc.c_str()) == 0) encryption (Enctryption::STARTTLS);
    if( strcasecmp ("tls", enc.c_str()) == 0) encryption (Enctryption::TLS);
    encryption (Enctryption::NONE);
}

void Smtp::sendmail(
        const std::vector<std::string> &to,
        const std::string& subject,
        const std::string& body) const
{
    std::ostringstream sbuf;

    sbuf << "From: ";
    sbuf << _from;
    sbuf << "\n";

    for( auto &it : to ) {
        sbuf << "To: ";
        sbuf << it;
        sbuf << "\n";
    }

    //NOTE: setLocale(LC_DATE, "C") should be called in outer scope
    sbuf << "Date: ";
    time_t t = ::time(NULL);
    struct tm* tmp = ::localtime(&t);
    char buf[256];
    strftime(buf, sizeof(buf), "%a, %d %b %Y %T %z\n", tmp);
    sbuf << buf;

    sbuf << "Subject: ";
    sbuf << subject;
    sbuf << "\n";

    sbuf << "\n";

    sbuf << body;
    sbuf << "\n";
    return sendmail(sbuf.str());
}

void Smtp::sendmail(
        const std::string& to,
        const std::string& subject,
        const std::string& body) const
{
    std::vector<std::string> recip;
    recip.push_back(to);
    return sendmail(recip, subject, body);
}


void Smtp::sendmail(
        const std::string& data)    const
{

    if (_has_fn) {
        _fn (data);
        return;
    }

    std::string cfg = createConfigFile();
    Argv argv = { _msmtp, "-t", "-C", cfg };
    SubProcess proc{argv, SubProcess::STDIN_PIPE | SubProcess::STDOUT_PIPE | SubProcess::STDERR_PIPE};

    bool bret = proc.run();
    if (!bret) {
        throw std::runtime_error( \
                _msmtp + " failed with exit code '" + \
                std::to_string(proc.getReturnCode()) + "'\nstderr:\n" + \
                read_all(proc.getStderr()));
    }

    ssize_t wr = ::write(proc.getStdin(), data.c_str(), data.size());
    if (wr != static_cast<ssize_t>(data.size())) {
        zsys_warning("Email truncated, exp '%zu', piped '%zd'", data.size(), wr);
    }
    ::close(proc.getStdin()); //EOF

    int ret = proc.wait();
    deleteConfigFile (cfg);
    if ( ret != 0 ) {
        throw std::runtime_error( \
                _msmtp + " wait with exit code '" + \
                std::to_string(proc.getReturnCode()) + "'\nstderr:\n" + \
                read_all(proc.getStderr()));
    }

    ret = proc.getReturnCode();
    if (ret != 0) {
        throw std::runtime_error( \
                _msmtp + " failed with exit code '" + \
                std::to_string(proc.getReturnCode()) + "'\nstderr:\n" + \
                read_all(proc.getStderr()));
    }

}

std::string
sms_email_address (
        const std::string& gw_template,
        const std::string& phone_number)
{
    std::string ret = gw_template;
    std::string clean_phone_number;
    for (const char ch : phone_number) {
        if (::isdigit (ch))
            clean_phone_number.push_back (ch);
    }

    ssize_t idx = clean_phone_number.size () -1;
    for (;;) {
        auto it = ret.find_last_of ('#');
        if (it == std::string::npos)
            break;
        if (idx < 0)
            throw std::logic_error ("Cannot apply number '" + phone_number + "' onto template '" + gw_template + "'. Not enough numbers in phone number");
        ret [it] = clean_phone_number [idx];
        idx --;
    }

    return ret;
}

int
    msmtp_stderr2code (
        const std::string &inp)
{
    if (inp.size () == 0)
        return static_cast<int> (SmtpError::Succeeded);

    return static_cast<int> (SmtpError::ServerUnreachable);
}


//  --------------------------------------------------------------------------
//  Self test of this class

void
email_test (bool verbose)
{
    printf (" * email: ");

    //  @selftest

    // test case 01 - normal operation
    std::string to = sms_email_address ("0#####@hyper.mobile", "+79 (0) 123456");
    assert (to == "023456@hyper.mobile");

    // test case 02 - not enough numbers
    try {
        to = sms_email_address ("0#####@hyper.mobile", "456");
        assert (false); // <<< due exception throwed up, we should not reach this place
    }
    catch (std::logic_error &e) {
    }

    // test case 03 - no # in template
    to = sms_email_address ("0^^^^^@hyper.mobile", "456");
    assert (to == "0^^^^^@hyper.mobile");

    // test case 04 empty template
    to = sms_email_address ("", "+79 (0) 123456");
    assert (to.empty ());

    // test case 05 empty number
    try {
        to = sms_email_address ("0#####@hyper.mobile", "");
        assert (false); // <<< due exception throwed up, we should not reach this place
    }
    catch (std::logic_error &e) {
    }

    //  @end
    printf ("OK\n");
}
