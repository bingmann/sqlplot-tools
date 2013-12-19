/******************************************************************************
 * src/fieldset.h
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

#ifndef FIELDSET_HEADER
#define FIELDSET_HEADER

#include <string>
#include <vector>
#include <utility>

//! List of field specifications to automatically detect SQL columns types
class FieldSet
{
public:
    //! automatically detected SQL column data types: larger ones are more
    //! specific, lower ones are more generic.
    enum fieldtype { T_NONE, T_VARCHAR, T_DOUBLE, T_INTEGER };

    //! return the SQL data type name for a field type
    static const char* sqlname(fieldtype t);

    //! detect the field type of a string
    static fieldtype detect(const std::string& str);

    //! self-verify field type detection
    static void check_detect();

protected:
    //! pair (key,field-type) field specifications
    typedef std::pair<std::string, fieldtype> sfpair_type;

    //! list of field specifications
    typedef std::vector<sfpair_type> fieldset_type;

    //! list of field specifications
    fieldset_type m_fieldset;

public:
    //! number of fields is set
    inline size_t count() const
    {
        return m_fieldset.size();
    }

    //! add new field (key,value), detect the value type and augment found type
    void add_field(const std::string& key, const std::string& value);

    //! return CREATE TABLE for the given fieldset
    std::string make_create_table(const std::string& tablename, bool temporary) const;
};

#endif // FIELDSET_HEADER
