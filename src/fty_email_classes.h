/*  =========================================================================
    fty_email_classes - private header file

    Copyright (C) 2014 - 2020 Eaton

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
################################################################################
#  THIS FILE IS 100% GENERATED BY ZPROJECT; DO NOT EDIT EXCEPT EXPERIMENTALLY  #
#  Read the zproject/README.md for information about making permanent changes. #
################################################################################
    =========================================================================
*/

#ifndef FTY_EMAIL_CLASSES_H_INCLUDED
#define FTY_EMAIL_CLASSES_H_INCLUDED

//  Platform definitions, must come first
#include "platform.h"

//  External API
#include "../include/fty_email.h"

//  Opaque class structures to allow forward references
#ifndef EMAILCONFIGURATION_T_DEFINED
typedef struct _emailconfiguration_t emailconfiguration_t;
#define EMAILCONFIGURATION_T_DEFINED
#endif
#ifndef EMAIL_T_DEFINED
typedef struct _email_t email_t;
#define EMAIL_T_DEFINED
#endif

//  Extra headers

//  Internal API

#include "emailconfiguration.h"
#include "email.h"

//  *** To avoid double-definitions, only define if building without draft ***
#ifndef FTY_EMAIL_BUILD_DRAFT_API

//  *** Draft method, defined for internal use only ***
//  Self test of this class.
FTY_EMAIL_PRIVATE void
    emailconfiguration_test (bool verbose);

//  *** Draft method, defined for internal use only ***
//  Self test of this class.
FTY_EMAIL_PRIVATE void
    email_test (bool verbose);

//  Self test for private classes
FTY_EMAIL_PRIVATE void
    fty_email_private_selftest (bool verbose, const char *subtest);

#endif // FTY_EMAIL_BUILD_DRAFT_API

#endif
