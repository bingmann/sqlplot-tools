/******************************************************************************
 * src/fieldset.cpp
 *
 * FieldSet class to automatically detect SQL column types from data set.
 *
 ******************************************************************************
 * Copyright (C) 2013 Timo Bingmann <tb@panthema.net>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#include "fieldset.h"
#include "common.h"

#include <cassert>
#include <sstream>

//! return the SQL data type name for a field type
const char* FieldSet::sqlname(fieldtype t)
{
    switch (t) {
    default:
    case T_NONE: return "NONE";
    case T_VARCHAR:
    {
        if (g_db->type() == SqlDatabase::DB_MYSQL)
            return "TEXT";

        return "VARCHAR";
    }
    case T_DOUBLE: return "DOUBLE PRECISION";
    case T_INTEGER: return "BIGINT";
    }
}

//! detect the field type of a string
FieldSet::fieldtype FieldSet::detect(const std::string& str)
{
    std::string::const_iterator it = str.begin();

    // skip sign
    if (it != str.end() && (*it == '+' || *it == '-')) ++it;

    // iterate over digits
    while (it != str.end() && isdigit(*it)) ++it;

    if (it == str.end() && it != str.begin()) {
        return T_INTEGER;
    }
        
    // skip decimal and iterate over digits
    if (it != str.end() && *it == '.') {
        ++it;

        // iterate over digits
        while (it != str.end() && isdigit(*it)) ++it;

        if (it == str.end() && it != str.begin()) {
            return T_DOUBLE;
        }

        // check double exponent
        if (it != str.end() && (*it == 'e' || *it == 'E')) {
            ++it;

            // skip sign
            if (it != str.end() && (*it == '+' || *it == '-')) ++it;

            // iterate over digits
            while (it != str.end() && isdigit(*it)) ++it;

            if (it == str.end() && it != str.begin()) {
                return T_DOUBLE;
            }
        }
    }

    return T_VARCHAR;
}

//! self-verify field type detection
void FieldSet::check_detect()
{
    assert( detect("1234") == T_INTEGER );
    assert( detect("1234.3") == T_DOUBLE );
    assert( detect(".3e-3") == T_DOUBLE );
    assert( detect("1234,3") == T_VARCHAR );
    assert( detect("sdfdf") == T_VARCHAR );
}

//! add new field (key,value), detect the value type and augment found type
void FieldSet::add_field(const std::string& key, const std::string& value)
{
    fieldtype t = detect(value);

    for (fieldset_type::iterator fi = m_fieldset.begin();
         fi != m_fieldset.end(); ++fi)
    {
        if (fi->first == key) // found matching entry
        {
            if (fi->second > t) {
                fi->second = t;
            }
            return;
        }
    }

    // add new entry
    m_fieldset.push_back( sfpair_type(key,t) );
}

//! return CREATE TABLE for the given fieldset
std::string FieldSet::make_create_table(const std::string& tablename, bool temporary) const
{
    std::ostringstream os;
    os << "CREATE "
       << (temporary ? "TEMPORARY " : "")
       << "TABLE " << g_db->quote_field(tablename) << " (";

    for (fieldset_type::const_iterator fi = m_fieldset.begin();
         fi != m_fieldset.end(); ++fi)
    {
        if (fi != m_fieldset.begin()) os << ", ";
        os << g_db->quote_field(fi->first) << ' ' << sqlname(fi->second);
    }

    os << ")";
    return os.str();
}
