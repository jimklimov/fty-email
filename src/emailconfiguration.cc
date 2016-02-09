/*  =========================================================================
    emailconfiguration - Class that is responsible for email configuration

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
    emailconfiguration - Class that is responsible for email configuration
@discuss
@end
*/

#include "agent_smtp_classes.h"


void ElementDetails::
    print (void)
{
    zsys_debug ("priority %c", _priority);
    zsys_debug ("name %s", _name.c_str());
    zsys_debug ("contact name %s", _contactName.c_str());
    zsys_debug ("contact email %s", _contactEmail.c_str());
}

static std::string
    replaceTokens (const std::string &text,
        const std::string &pattern,
        const std::string &replacement)
{
    std::string result = text;
    size_t pos = 0;
    while( ( pos = result.find(pattern, pos) ) != std::string::npos){
        result.replace(pos, pattern.length(), replacement);
        pos += replacement.length();
    }
    return result;
}


std::string EmailConfiguration::
    generateEmailBodyResolved (const AlertDescription &alert,
        const ElementDetails &asset,
        const std::string &ruleName)
{
    std::string result = _emailBodyResolvedAlertTemplate;
    result = replaceTokens (result, "${rulename}", ruleName);
    result = replaceTokens (result, "${assetname}", asset._name);
    result = replaceTokens (result, "${description}", alert._description);
    return result;
}


std::string EmailConfiguration::
    generateEmailBodyActive (const AlertDescription &alert,
        const ElementDetails &asset,
        const std::string &ruleName)
{
    std::string result = _emailBodyActiveAlertTemplate;
    result = replaceTokens (result, "${rulename}", ruleName);
    result = replaceTokens (result, "${assetname}", asset._name);
    result = replaceTokens (result, "${description}", alert._description);
    result = replaceTokens (result, "${priority}", std::to_string(asset._priority));
    result = replaceTokens (result, "${severity}", alert._severity);
    result = replaceTokens (result, "${state}", alert._state);
    return result;
}


std::string EmailConfiguration::
    generateEmailSubjectResolved (const AlertDescription &alert,
        const ElementDetails &asset,
        const std::string &ruleName)
{
    std::string result = _emailSubjectResolvedAlertTemplate;
    result = replaceTokens (result, "${rulename}", ruleName);
    result = replaceTokens (result, "${assetname}", asset._name);
    return result;
}


std::string EmailConfiguration::
    generateEmailSubjectActive (const AlertDescription &alert,
        const ElementDetails &asset,
        const std::string &ruleName)
{
    std::string result = _emailSubjectActiveAlertTemplate;
    result = replaceTokens (result, "${rulename}", ruleName);
    result = replaceTokens (result, "${assetname}", asset._name);
    result = replaceTokens (result, "${description}", alert._description);
    result = replaceTokens (result, "${priority}", std::to_string(asset._priority));
    result = replaceTokens (result, "${severity}", alert._severity);
    result = replaceTokens (result, "${state}", alert._state);
    return result;
}


std::string EmailConfiguration::
    generateBody (const AlertDescription &alert,
        const ElementDetails &asset,
        const std::string &ruleName)
{
    if ( alert._state == "RESOLVED" ) {
        zsys_debug ("RESOLVED BODY");
        return generateEmailBodyResolved (alert, asset, ruleName);
    }
    else {
        zsys_debug ("ACTIVE BODY");
        return generateEmailBodyActive (alert, asset, ruleName);
    }
}


std::string EmailConfiguration::
    generateSubject (const AlertDescription &alert,
        const ElementDetails &asset,
        const std::string &ruleName)
{
    if ( alert._state == "RESOLVED" ) {
        return generateEmailSubjectResolved (alert, asset, ruleName);
    }
    else {
        return generateEmailSubjectActive (alert, asset, ruleName);
    }
}


const std::string EmailConfiguration::
    _emailBodyActiveAlertTemplate = "In the system an alert was detected. \n"
        " Source rule: ${rulename}\n"
        " Asset: ${assetname}\n"
        " Alert priority: P${priority}\n"
        " Alert severity: ${severity}\n"
        " Alert description: ${description}\n"
        " Alert state: ${state}";


const std::string EmailConfiguration::
    _emailSubjectActiveAlertTemplate = "${severity} alert on ${assetname} "
        "from the rule ${rulename} is active!";


const std::string EmailConfiguration::
    _emailBodyResolvedAlertTemplate = "In the system an alert was resolved."
        " \n Source rule: ${rulename}\n"
        " Asset: ${assetname}\n "
        " Alert description: ${description}";


const std::string EmailConfiguration::
    _emailSubjectResolvedAlertTemplate = "Alert on ${assetname} "
        "from the rule ${rulename} was resolved";


void
emailconfiguration_test (bool verbose)
{
    printf (" * emailconfiguration: ");
    printf ("OK\n");
}
