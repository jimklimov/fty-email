/*  =========================================================================
    emailconfiguration - Class that is responsible for email configuration

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
    emailconfiguration - Class that is responsible for email configuration
@discuss
@end
*/

#include "fty_email_classes.h"

#define BODY_ACTIVE \
"In the system an alert was detected.\n\
Source rule: ${rulename}\n\
Asset: ${assetname}\n\
Alert priority: P${priority}\n\
Alert severity: ${severity}\n\
Alert description: ${description}\n\
Alert state: ${state}"

#define SUBJECT_ACTIVE \
"${severity} alert on ${assetname}\n\
from the rule ${rulename} is active!"

#define BODY_RESOLVED \
"In the system an alert was resolved.\n\
Source rule: ${rulename}\n\
Asset: ${assetname}\n\
Alert description: ${description}"

#define SUBJECT_RESOLVED \
"Alert on ${assetname} \n\
from the rule ${rulename} was resolved"


// ----------------------------------------------------------------------------
// static helper functions

static std::string
replace_tokens (
        const std::string& text,
        const std::string& pattern,
        const std::string& replacement)
{
    std::string result = text;
    size_t pos = 0;
    while( ( pos = result.find(pattern, pos) ) != std::string::npos){
        result.replace(pos, pattern.length(), replacement);
        pos += replacement.length();
    }
    return result;
}

static std::string
s_generateEmailBodyResolved (fty_proto_t *alert, const std::string& extname)
{
    std::string result (BODY_RESOLVED);
    result = replace_tokens (result, "${rulename}", fty_proto_rule (alert));
    result = replace_tokens (result, "${assetname}", extname);
    result = replace_tokens (result, "${description}", fty_proto_description (alert));
    return result;
}

static std::string
s_generateEmailBodyActive (fty_proto_t *alert, const std::string& priority, const std::string& extname)
{
    std::string result = BODY_ACTIVE;
    result = replace_tokens (result, "${rulename}", fty_proto_rule (alert));
    result = replace_tokens (result, "${assetname}", extname);
    result = replace_tokens (result, "${description}", fty_proto_description (alert));
    result = replace_tokens (result, "${priority}", priority);
    result = replace_tokens (result, "${severity}", fty_proto_severity (alert));
    result = replace_tokens (result, "${state}", fty_proto_state (alert));
    return result;
}

static std::string
s_generateEmailSubjectResolved (fty_proto_t *alert, const std::string &extname)
{
    std::string result = SUBJECT_RESOLVED;
    result = replace_tokens (result, "${rulename}", fty_proto_rule (alert));
    result = replace_tokens (result, "${assetname}", extname);
    return result;
}

static std::string
s_generateEmailSubjectActive (fty_proto_t *alert, const std::string& priority, const std::string& extname)
{
    std::string result = SUBJECT_ACTIVE;
    result = replace_tokens (result, "${rulename}", fty_proto_rule (alert));
    result = replace_tokens (result, "${assetname}", extname);
    result = replace_tokens (result, "${description}", fty_proto_description (alert));
    result = replace_tokens (result, "${priority}", priority);
    result = replace_tokens (result, "${severity}", fty_proto_severity (alert));
    result = replace_tokens (result, "${state}", fty_proto_state (alert));
    return result;
}

// ----------------------------------------------------------------------------
// header functions

std::string
generate_body (fty_proto_t *alert, const std::string& priority, const std::string& extname)
{
    if (streq (fty_proto_state (alert), "RESOLVED")) {
        return s_generateEmailBodyResolved (alert, extname);
    }
    return s_generateEmailBodyActive (alert, priority, extname);
}

std::string
generate_subject (fty_proto_t *alert, const std::string& priority, const std::string& extname)
{
    if (streq (fty_proto_state (alert), "RESOLVED")) {
        return s_generateEmailSubjectResolved (alert, extname);
    }
    return s_generateEmailSubjectActive (alert, priority, extname);
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
emailconfiguration_test (bool verbose)
{
    printf (" * emailconfiguration: ");
    // TODO
    //  * replace_tokens
    //  * generate_subject
    //  * generate_body
    printf ("OK\n");
}
