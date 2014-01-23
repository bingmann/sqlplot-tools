/******************************************************************************
 * src/sql.h
 *
 * Encapsulate SQL queries into a generic C++ class, and specialize for
 * different SQL implementations.
 *
 ******************************************************************************
 * Copyright (C) 2014 Timo Bingmann <tb@panthema.net>
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

#ifndef SQL_HEADER
#define SQL_HEADER

#include <string>
#include <vector>
#include <map>

#include <boost/shared_ptr.hpp>

class SqlQueryImpl
{
private:
    //! Saved query string
    std::string m_query;

    //! Type of m_colmap
    typedef std::map<std::string, unsigned int> colmap_type;

    //! Mapping column name -> number
    colmap_type m_colmap;

public:
    
    //! Execute a SQL query, throws on errors.
    SqlQueryImpl(const std::string& query);

    //! Free result
    virtual ~SqlQueryImpl();

    //! Return query string.
    const std::string& query() const;

    //! Return number of rows in result, throws if no tuples.
    virtual unsigned int num_rows() const = 0;

    //! Return number of columns in result, throws if no tuples.
    virtual unsigned int num_cols() const = 0;

    // *** Column Name Mapping ***

    //! Return column name of col
    virtual const char* col_name(unsigned int col) const = 0;

    //! Read column name map for the following col -> num mappings.
    virtual void read_colmap();

    //! Check if a column name exists.
    virtual bool exist_col(const std::string& name) const;

    //! Returns column number of name or throws if it does not exist.
    virtual unsigned int find_col(const std::string& name) const;

    // *** Result Iteration ***

    //! Return the current row number.
    virtual unsigned int current_row() const = 0;

    //! Advance current result row to next (or first if uninitialized)
    virtual bool step() = 0;

    //! Returns true if cell (current_row,col) is NULL.
    virtual bool isNULL(unsigned int col) const = 0;

    //! Return text representation of column col of current row.
    virtual const char* text(unsigned int col) const = 0;

    // *** Complete Result Caching ***

    //! read complete result into memory
    virtual void read_complete() = 0;

    //! Returns true if cell (row,col) is NULL.
    virtual bool isNULL(unsigned int row, unsigned int col) const = 0;

    //! Return text representation of cell (row,col).
    virtual const char* text(unsigned int row, unsigned int col) const = 0;

    // *** TEXTTABLE formatting ***

    //! format result as a text table
    std::string format_texttable();
};

//! shared pointer to an SqlQuery implementation
typedef boost::shared_ptr<SqlQueryImpl> SqlQuery;

//! abstract SqlDatabase class, provides mainly queries.
class SqlDatabase
{
public:
    //! virtual destructor to free connection
    virtual ~SqlDatabase();

    //! try to connect to the database with default parameters
    virtual bool initialize() = 0;

    //! return string for the i-th placeholder, where i starts at 0.
    virtual std::string placeholder(unsigned int i) const = 0;

    //! construct query object for given string
    virtual SqlQuery query(const std::string& query) = 0;

    //! construct query object for given string with placeholder parameters
    virtual SqlQuery query(const std::string& query,
                           const std::vector<std::string>& params) = 0;

    //! test if a table exists in the database
    virtual bool exist_table(const std::string& table) = 0;

    //! return last error message string
    virtual const char* errmsg() const = 0;
};

#endif // SQL_HEADER
