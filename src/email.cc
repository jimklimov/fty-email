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
    _from {},
    _encryption { Enctryption::NONE },
    _username {},
    _password {},
    _msmtp { "/usr/bin/msmtp" }
{
    /*
    _argv = Argv{
        "/usr/bin/msmtp",
        "--host=" + _host,
        "--protocol=smtp",
        "--tls=off",
        "--auto-from=off",
        "--read-recipients",
        "--read-envelope-from",
        "--auth=off"};
    */
}

std::string Smtp::createConfigFile() const
{
    char filename[] = "/tmp/bios-msmtp-XXXXXX.cfg";
    int handle = mkstemps(filename,4);
    std::string line;

    line = "defaults\n";
    line += "host " + _host +"\n";
    line += "from " + _from + "\n";
    switch (_encryption) {
    case Enctryption::NONE:
        line += "tls off\n"
                "tls_starttls off\n";
        break;
    case Enctryption::TLS:
        line += "tls on\n"
                "tls_certcheck off\n";
        break;
    case Enctryption::STARTTLS:
        // TODO: is this correct?
        line += "tls off\n"
                "tls_certcheck off\n"
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
    write (handle,  line.c_str(), line.size());
    close (handle);
    return std::string(filename);
}

void Smtp::deleteConfigFile(std::string &filename) const
{
    unlink (filename.c_str());
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
    std::string cfg = createConfigFile();
    Argv argv = { _msmtp, "-C", cfg };
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


//  --------------------------------------------------------------------------
//  Self test of this class

void
email_test (bool verbose)
{
    printf (" * email: ");

    //  @selftest
    //  Simple create/destroy test
    //  @end
    printf ("OK\n");
}
