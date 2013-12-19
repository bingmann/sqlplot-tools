/******************************************************************************
 * src/strtools.h
 *
 * Generic string tools for std::string.
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

#ifndef STRTOOLS_HEADER
#define STRTOOLS_HEADER

#include <string>
#include <ostream>

/**
 * Trims the given string on the left and right. Removes all characters in the
 * given drop array, which defaults to " ". Returns a copy of the string.
 *
 * @param str	string to process
 * @param drop	remove these characters
 * @return	new trimmed string
 */
static inline std::string trim(const std::string& str, const std::string& drop = " ")
{
    std::string::size_type pos1 = str.find_first_not_of(drop);
    if (pos1 == std::string::npos) return std::string();

    std::string::size_type pos2 = str.find_last_not_of(drop);
    if (pos2 == std::string::npos) return std::string();

    return str.substr(pos1 == std::string::npos ? 0 : pos1,
                      pos2 == std::string::npos ? (str.size() - 1) : (pos2 - pos1 + 1));
}

/**
 * Checks if the given match string is located at the start of this string.
 */
static inline bool is_prefix(const std::string& str, const std::string& match)
{
    if (match.size() > str.size()) return false;
    return std::equal( match.begin(), match.end(), str.begin() );
}

/**
 * Shorten a string to width charaters, adding "..." at the end.
 */
static inline std::string shorten(const std::string& str, size_t width = 80)
{
    if (str.size() > width)
        return str.substr(0, width - 3) + "...";
    else
        return str;
}

/**
 * Split the given string by whitespaces into distinct words. Multiple
 * consecutive whitespaces are considered as one split point. Whitespaces are
 * space, tab, newline and carriage-return.
 *
 * @param str	string to split
 * @param limit	maximum number of parts returned
 * @return	vector containing each split substring
 */
static inline std::vector<std::string> split_ws(const std::string& str, std::string::size_type limit = std::string::npos)
{
    std::vector<std::string> out;
    if (limit == 0) return out;

    std::string::const_iterator it = str.begin(), last = it;

    for (; it != str.end(); ++it)
    {
	if (*it == ' ' || *it == '\n' || *it == '\t' || *it == '\r')
	{
	    if (it == last) { // skip over empty split substrings
		last = it+1;
		continue;
	    }

	    if (out.size() + 1 >= limit)
	    {
		out.push_back(std::string(last, str.end()));
		return out;
	    }

	    out.push_back(std::string(last, it));
	    last = it + 1;
	}
    }

    if (last != it)
	out.push_back(std::string(last, it));

    return out;
}

/**
 * Read a complete stream into a std::string
 */
static inline std::string read_stream(std::istream& is)
{
    std::string out;

    char buffer[8192];
    while ( is.read(buffer, sizeof(buffer)) )
    {
        out.append(buffer, is.gcount());
    }

    out.append(buffer, is.gcount());

    return out;
}

#endif // STRTOOLS_HEADER
