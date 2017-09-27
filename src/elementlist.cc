/*  =========================================================================
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

#include "fty_email_classes.h"

#include <utility>
#include <cstdint>
#include <stdexcept>
#include <fstream>
#include <cxxtools/jsonserializer.h>
#include <cxxtools/jsondeserializer.h>
#include <czmq.h>
#include "email.h"

#include "elementlist.h"

const std::string ElementList::DEFAULT_PATH_TO_FILE = "/var/lib/fty/fty-email/state";

void operator<<= (cxxtools::SerializationInfo& si, const Element& element)
{
    si.addMember("name") <<= element.name;
    si.addMember("extname") <<= element.extname;
    si.addMember("priority") <<= std::to_string (element.priority); // ARM workaround
    si.addMember("contact_name") <<= element.contactName;
    si.addMember("contact_email") <<= element.email;
    si.addMember("contact_phone") <<= element.phone;
}

void operator>>= (const cxxtools::SerializationInfo& si, Element& asset)
{
    // ARM workaround
    std::string temp;
    si.getMember("priority") >>= temp;
    try {
        uint64_t utemp = std::stoul (temp);
        if (utemp > UINT8_MAX)
            throw std::logic_error ("");
        asset.priority = std::stoul (temp);
    }
    catch (...) {
        asset.priority = 0;
    }

    si.getMember("name") >>= asset.name;
    si.getMember("extname") >>= asset.extname;
    si.getMember("contact_name") >>= asset.contactName;
    si.getMember("contact_email") >>= asset.email;
    si.getMember("contact_phone") >>= asset.phone;
}

bool ElementList::get (const std::string& asset_name, Element& element) const
{
    try {
        element = _assets.at (asset_name);
    }
    catch (const std::out_of_range& e) {
        return false;
    }
    return true;
}
unsigned int ElementList::size (void) const
{
    return _assets.size();
}
void ElementList::add (const Element& element)
{
    auto search = _assets.find (element.name);
    if (search == _assets.cend ()) {
        _assets.emplace (std::make_pair (element.name, element));
    }
    else {
        search->second = element;
    }
}

void ElementList::remove (const char *asset_name) {
    _assets.erase(asset_name);
}

void ElementList::updateContactName (const std::string &elementName, const std::string &contactName)
{
    auto search = _assets.find (elementName);
    if ( search != _assets.cend ()) {
        search->second.contactName = contactName;
    }
}

void ElementList::updateExtName (const std::string &elementName, const std::string &extName)
{
    auto search = _assets.find (elementName);
    if ( search != _assets.cend ()) {
        search->second.extname = extName;
    }
}

void ElementList::updateEmail (const std::string &elementName, const std::string &email)
{
    auto search = _assets.find (elementName);
    if ( search != _assets.cend ()) {
        search->second.email = email;
    }
}

void ElementList::updatePhone (const std::string &elementName, const std::string &phone)
{
    auto search = _assets.find (elementName);
    if ( search != _assets.cend ()) {
        search->second.phone = phone;
    }
}

void ElementList::updateSMSEmail (const std::string &elementName, const std::string &email)
{
    auto search = _assets.find (elementName);
    if ( search != _assets.cend ()) {
        search->second.sms_email = email;
    }
}

bool ElementList::exists (const std::string& asset_name) const
{
    auto search =_assets.find (asset_name);
    if (search != _assets.cend ())
        return true;
    return false;
}

bool ElementList::empty () const
{
    return _assets.empty ();
}

void ElementList::setFile (const std::string& path_to_file)
{
    if (_path_set == false) {
        _path = path_to_file;
        _path_set = true;
    }
}

void ElementList::setFile ()
{
    if (_path_set == false) {
        _path = DEFAULT_PATH_TO_FILE;
        _path_set = true;
    }
}

int ElementList::save () {
    setFile ();
    std::ofstream ofs (_path + ".new", std::ofstream::out);
    if ( !ofs.good() ) {
        zsys_error ("Cannot open file '%s' for write", (_path + ".new").c_str());
        ofs.close();
        return -1;
    }
    int r = std::rename (std::string ( _path).append(".new").c_str (),
        std::string (_path.c_str ()).c_str());
    if ( r != 0 ) {
        zsys_error ("Cannot rename file '%s' to '%s'", _path.c_str(), _path.c_str());
        return -2;
    }
    ofs << serialize_to_json();
    ofs.close();
    return 0;
}

int ElementList::load (const std::string &sms_gateway) {
    // TODO if !is file
    std::ifstream ifs (_path, std::ios::in | std::ios::binary);
    if ( !ifs.good() ) {
        zsys_error ("Cannot open file '%s' for read", _path.c_str());
        ifs.close();
        return -1;
    }
    try {
        cxxtools::SerializationInfo si;
        std::string json_string(std::istreambuf_iterator<char>(ifs), {});
        std::stringstream s(json_string);
    // TODO try
        cxxtools::JsonDeserializer json(s);
        json.deserialize(si);
        si >>= _assets;
        ifs.close();

        for ( auto &it : _assets ) {
            try {
                it.second.sms_email = sms_email_address (sms_gateway, it.second.phone);
            }
            catch ( const std::exception &e ) {
                zsys_error (e.what());
            }
        }
        return 0;
    }
    catch ( const std::exception &e) {
        zsys_error ("Starting without initial state. Cannot deserialize the file '%s'. Error: '%s'", _path.c_str(), e.what());
        ifs.close();
        return -1;
    }
}

std::string ElementList::serialize_to_json () const
{
    std::stringstream s;
    cxxtools::JsonSerializer js (s);
    js.beautify (true);
    js.serialize (_assets).finish();
    return s.str();
}

void Element::debug_print () const
{
    zsys_debug ("name = '%s'", name.c_str ());
    zsys_debug ("extname = '%s'", extname.c_str ());
    zsys_debug ("priority = '%d'", priority);
    zsys_debug ("contact name = '%s'", contactName.c_str ());
    zsys_debug ("contact email = '%s'", email.c_str ());
    zsys_debug ("contact phone = '%s'", phone.c_str ());
}

void
elementlist_test (bool verbose)
{
    printf (" * elementlist: ");

    //  @selftest
    //  Simple create/destroy test
    //  @end
    printf ("OK\n");
}
