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
protected:
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
    virtual std::string col_name(unsigned int col) const = 0;

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
    virtual std::string text(unsigned int col) const = 0;

    // *** Complete Result Caching ***

    //! read complete result into memory
    virtual void read_complete() = 0;

    //! Returns true if cell (row,col) is NULL.
    virtual bool isNULL(unsigned int row, unsigned int col) const = 0;

    //! Return text representation of cell (row,col).
    virtual std::string text(unsigned int row, unsigned int col) const = 0;

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
    //! enum describing supported SQL databases
    enum db_type { DB_PGSQL, DB_MYSQL, DB_SQLITE };

public:
    //! virtual destructor to free connection
    virtual ~SqlDatabase();

    //! return type of SQL database
    virtual db_type type() const = 0;

    //! try to connect to the database with given parameters
    virtual bool initialize(const std::string& params) = 0;

    //! return string for the i-th placeholder, where i starts at 0.
    virtual std::string placeholder(unsigned int i) const = 0;

    //! return quoted table or field identifier
    virtual std::string quote_field(const std::string& field) const = 0;

    //! execute SQL query without result
    virtual bool execute(const std::string& query) = 0;

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

//! Cache complete data from SQL results
class SqlDataCache
{
protected:

    //! complete table read
    bool m_complete;

    //! type of each row
    typedef std::vector< std::pair<bool,std::string> > row_type;

    //! type of the whole table
    typedef std::vector<row_type> table_type;

    //! cache table
    table_type m_table;

protected:
    //! simple initializer
    SqlDataCache()
        : m_complete(false)
    {
    }

    //! cache complete data of a query
    void read_complete(SqlQueryImpl& sql)
    {
        if (m_complete) return;

        while (sql.step())
        {
            row_type row;

            for (size_t col = 0; col < sql.num_cols(); ++col)
            {
                if (sql.isNULL(col))
                    row.push_back( std::make_pair(true, std::string()) );
                else
                    row.push_back( std::make_pair(false, sql.text(col)) );
            }

            m_table.push_back(row);
        }

        m_complete = true;
    }

    //! Test if complete result set is cached
    bool is_complete() const
    {
        return m_complete;
    }

    //! Return number of cached rows
    size_t num_rows() const
    {
        return m_table.size();
    }

    //! Returns true if cell (row,col) is NULL.
    bool isNULL(unsigned int row, unsigned int col) const
    {
        assert(m_complete);
        assert(row < m_table.size());
        assert(col < m_table[row].size());
        return m_table[row][col].first;
    }

    //! Return text representation of cell (row,col).
    std::string text(unsigned int row, unsigned int col) const
    {
        assert(m_complete);
        assert(row < m_table.size());
        assert(col < m_table[row].size());
        return m_table[row][col].second.c_str();
    }
};

#endif // SQL_HEADER
