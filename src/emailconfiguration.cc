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
s_generateEmailBodyResolved (const Alert& alert, const Element& asset)
{
    std::string result (BODY_RESOLVED);
    result = replace_tokens (result, "${rulename}", alert.rule);
    result = replace_tokens (result, "${assetname}", asset.name);
    result = replace_tokens (result, "${description}", alert.description);
    return result;
}

static std::string
s_generateEmailBodyActive (const Alert& alert, const Element& asset)
{
    std::string result = BODY_ACTIVE;
    result = replace_tokens (result, "${rulename}", alert.rule);
    result = replace_tokens (result, "${assetname}", asset.name);
    result = replace_tokens (result, "${description}", alert.description);
    result = replace_tokens (result, "${priority}", std::to_string (asset.priority));
    result = replace_tokens (result, "${severity}", alert.severity);
    result = replace_tokens (result, "${state}", alert.state);
    return result;
}

static std::string
s_generateEmailSubjectResolved (const Alert& alert, const Element& asset)
{
    std::string result = SUBJECT_RESOLVED;
    result = replace_tokens (result, "${rulename}", alert.rule);
    result = replace_tokens (result, "${assetname}", asset.name);
    return result;
}

static std::string
s_generateEmailSubjectActive (const Alert& alert, const Element& asset)
{
    std::string result = SUBJECT_ACTIVE;
    result = replace_tokens (result, "${rulename}", alert.rule);
    result = replace_tokens (result, "${assetname}", asset.name);
    result = replace_tokens (result, "${description}", alert.description);
    result = replace_tokens (result, "${priority}", std::to_string(asset.priority));
    result = replace_tokens (result, "${severity}", alert.severity);
    result = replace_tokens (result, "${state}", alert.state);
    return result;
}

// ----------------------------------------------------------------------------
// header functions

std::string
generate_body (const Alert& alert, const Element& asset)
{
    if (alert.state == "RESOLVED") {
        return s_generateEmailBodyResolved (alert, asset);
    }
    return s_generateEmailBodyActive (alert, asset);
}

std::string
generate_subject (const Alert& alert, const Element& asset)
{
    if (alert.state == "RESOLVED") {
        return s_generateEmailSubjectResolved (alert, asset);
    }
    return s_generateEmailSubjectActive (alert, asset);
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

