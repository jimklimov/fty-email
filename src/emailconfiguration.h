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

#ifndef EMAILCONFIGURATION_H_INCLUDED
#define EMAILCONFIGURATION_H_INCLUDED

#include "email.h"
#include <string>
#include <set>


struct AlertDescription_ {
    std::string _description;
    std::string _state;
    std::string _severity;
    std::set<std::string> _actions;
    int64_t _timestamp;
    int64_t _lastNotification;
    int64_t _lastUpdate;

    AlertDescription_(
        const std::string &description,
        const std::string &state,
        const std::string &severity,
        const std::set<std::string> &actions,
        int64_t timestamp) :
            _description (description),
            _state (state),
            _severity (severity),
            _actions (actions),
            _timestamp (timestamp),
            _lastNotification (0),
            _lastUpdate (timestamp)
    {
    };
};
typedef struct AlertDescription_ AlertDescription;

struct ElementDetails_ {
    char _priority;
    std::string _name;
    // TODO be ready for the list of alerts
    std::string _contactName;
    std::string _contactEmail;
};

typedef struct ElementDetails_ ElementDetails;


/*
 * \brief Class that represents an email configuration and
 * is sesponsible for sending the mail
 *
 * TECHNICAL NOTE:
 * All necessary configure information should be immediately
 * propagated to the Smtp class
 */
class EmailConfiguration {
public:


    /*
     * \brief Create an empty configuration
     *
     * State is "unconfigured"
     */
    explicit EmailConfiguration() :
        _smtp{},
        _configured{false}

    {
    }


    /*
     * \brief if the configuration is already usable
     *
     * \return true  - if it is possible to send email
     *         false - if at least one parameter is missing
     */
    bool isConfigured(void) const {
        return _configured;
    }


    /* \brief Set the host
     *
     * \param host - a host name
     */
    void host (const std::string &host) {
        // TODO set the indicator, that it was configured
        _smtp.host (host);
    }


    /* \brief Set the sender
     *
     * \param from - a sender's email
     */
    void from (const std::string &from) {
        // TODO set the indicator, that it was configured
        _smtp.host (from);
    }


    //TODO: make me private
    Smtp _smtp;


    /*
     * \brief Generate an email body supposed to be send
     *
     * \param alert - an information about the alert
     * \param asset - an information about the asset
     * \param ruleName - a rule name
     *
     * \return string that contains the the body of the email
     */
    static std::string
        generateBody (const AlertDescription &alert,
            const ElementDetails &asset,
            const std::string &ruleName);


    /*
     * \brief Generate an email subject supposed to be send
     *
     * \param alert - an information about the alert
     * \param asset - an information about the asset
     * \param ruleName - a rule name
     *
     * \return string that contains the the subject of the email
     */
    static std::string
        generateSubject (const AlertDescription &alert,
            const ElementDetails &asset,
            const std::string &ruleName);

private:

    /*
     * \brief Generate an email body supposed to be send
     * for active alert
     *
     * \param alert - an information about the alert
     * \param asset - an information about the asset
     * \param ruleName - a rule name
     *
     * \return string that contains the the body of the email
     */
    static std::string
        generateEmailBodyActive (const AlertDescription &alert,
            const ElementDetails &asset,
            const std::string &ruleName);


    /*
     * \brief Generate an email body supposed to be send
     * for resolved alert
     *
     * \param alert - an information about the alert
     * \param asset - an information about the asset
     * \param ruleName - a rule name
     *
     * \return string that contains the the body of the email
     */
    static std::string
        generateEmailBodyResolved (const AlertDescription &alert,
        const ElementDetails &asset,
        const std::string &ruleName);


    /*
     * \brief Generate an email subject supposed to be send
     * for active alert
     *
     * \param alert - an information about the alert
     * \param asset - an information about the asset
     * \param ruleName - a rule name
     *
     * \return string that contains the the subject of the email
     */
    static  std::string
        generateEmailSubjectActive (const AlertDescription &alert,
        const ElementDetails &asset,
        const std::string &ruleName);


    /*
     * \brief Generate an email subject supposed to be send
     * for resolved alert
     *
     * \param alert - an information about the alert
     * \param asset - an information about the asset
     * \param ruleName - a rule name
     *
     * \return string that contains the the subject of the email
     */
    static std::string
        generateEmailSubjectResolved (const AlertDescription &alert,
        const ElementDetails &asset,
        const std::string &ruleName);

    static const std::string _emailBodyActiveAlertTemplate;
    static const std::string _emailSubjectActiveAlertTemplate;
    static const std::string _emailBodyResolvedAlertTemplate;
    static const std::string _emailSubjectResolvedAlertTemplate;

    // flag, that shows if everythinmg is configured
    bool _configured;
};


#endif
