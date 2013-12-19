/******************************************************************************
 * src/textlines.h
 *
 * Class to work with line-based text files.
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

#ifndef TEXTLINES_HEADER
#define TEXTLINES_HEADER

//! Class to work with text files line by line.
class TextLines
{
protected:

    //! line container type
    typedef std::vector<std::string> slist_type;

    //! array of lines in text file
    slist_type m_lines;

public:
 
    //! return number of lines
    size_t size() const
    {
        return m_lines.size();
    }

    //! return const reference to a line
    const std::string& operator[] (size_t i) const
    {
        assert(i < m_lines.size());
        return m_lines[i];
    }

    //! replace lines [begin,end) with content (or type desc)
    void replace(size_t begin, size_t end, const std::vector<std::string>& content,
                 const std::string& desc)
    {
        if (begin == end)
            OUT("Inserting " << desc << " at line " << begin);
        else
            OUT("Replace lines [" << begin << "," << end << ") with " << desc);

        m_lines.erase(m_lines.begin() + begin,
                      m_lines.begin() + end);

        m_lines.insert(m_lines.begin() + begin,
                       content.begin(), content.end());
    }

    //! replace lines [begin,end) with content (or type desc)
    void replace(size_t begin, size_t end, const std::string& content,
                 const std::string& desc)
    {
        slist_type clist;

        std::string line;
        std::istringstream is(content);
        while ( std::getline(is,line) )
        {
            clist.push_back(line);
        }

        return replace(begin, end, clist, desc);
    }

    //! read complete file line-wise
    void read_stream(std::istream& is)
    {
        m_lines.clear();

        std::string line;
        while ( std::getline(is,line) )
        {
            m_lines.push_back(line);
        }
    }

    //! write all lines to stream
    void write_stream(std::ostream& os) const
    {
        // output line
        for (slist_type::const_iterator i = m_lines.begin();
             i != m_lines.end(); ++i)
        {
            os << *i << std::endl;
        }
    }
};

#endif // TEXTLINES_HEADER
