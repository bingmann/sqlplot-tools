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

#include "strtools.h"
#include <cassert>

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
    const std::string& line(size_t i) const
    {
        assert(i < m_lines.size());
        return m_lines[i];
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

    //! replace lines [begin,end) with indented content (or type desc)
    void replace(size_t begin, size_t end, size_t indent,
                 const std::vector<std::string>& content,
                 const std::string& desc)
    {
        if (indent == 0) return replace(begin, end, content, desc);

        std::vector<std::string> ccopy(content.size());
        for (size_t si = 0; si < content.size(); ++si)
        {
            ccopy[si] = std::string(indent, ' ') + content[si];
        }

        return replace(begin, end, ccopy, desc);
    }

    //! replace lines [begin,end) with indented content (or type desc)
    void replace(size_t begin, size_t end, size_t indent,
                 const std::string& content, const std::string& desc)
    {
        slist_type clist;

        std::string line;
        std::istringstream is(content);
        while ( std::getline(is,line) )
        {
            clist.push_back(std::string(indent, ' ') + line);
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

    //! check for comment line prefixed with given character, returns index of
    //! comment char or -1 if the line is not a comment
    template <char CommentChar>
    static inline int is_comment_line(const std::string& line, size_t rep = 1)
    {
        int i = 0;
        while (isblank(line[i])) ++i;
        for (size_t r = 0; r < rep; ++r)
            if (line[i+r] != CommentChar) return -1;
        return i;
    }

    //! check for comment line prefixed with given character, returns index of
    //! comment char or -1 if the line is not a comment
    template <char CommentChar>
    inline int is_comment_line(size_t ln, size_t rep = 1) const
    {
        return is_comment_line<CommentChar>(line(ln), rep);
    }

    //! scan for next comment line with given prefix
    template <char CommentChar>
    ssize_t scan_for_comment(size_t ln, const std::string& cprefix) const
    {
        // scan lines forward till next comment line
        size_t cln = ln;
        while ( cln < size() && is_comment_line<CommentChar>(cln) < 0 )
        {
            ++cln;
        }

        // EOF reached -> no matching comment lien
        if ( cln >= size() )
            return -1;

        std::string comment = line(cln).substr(
            is_comment_line<CommentChar>(cln)+1
            );
        comment = trim(comment);

        return is_prefix(comment, cprefix) ? cln : -1;
    }

    //! try to collect an aligned comment block, advances ln at least one, and
    //! maybe more for a multiline command.
    template <char CommentChar>
    bool collect_comment(size_t& ln, std::string& out_cmd,
                         size_t& out_indent) const
    {
        // try to collect an aligned comment block
        int indent = is_comment_line<CommentChar>(ln);
        if (indent < 0) {
            ++ln; // not a comment
            return false;
        }

        std::string cmd = line(ln++).substr(indent+1);

        // multi-line command prefixed with two comment chars
        if (cmd[0] == CommentChar)
        {
            // trim second comment char
            cmd = cmd.substr(1);

            // collect lines while they are at the same indentation level
            while ( ln < m_lines.size() &&
                    is_comment_line<CommentChar>(ln, 2) == indent )
            {
                cmd += m_lines[ln++].substr(indent+2);
            }
        }

        out_cmd = trim(cmd);
        out_indent = indent;
        return true;
    }
};

#endif // TEXTLINES_HEADER
