/******************************************************************************
 * src/sqlplot-tools.cpp
 *
 * Process embedded SQL plot instructions in LaTeX or Gnuplot files.
 *
 ******************************************************************************
 * Copyright (C) 2013-2016 Timo Bingmann <tb@panthema.net>
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

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include <unistd.h>

#include "simpleopt.h"
#include "common.h"
#include "strtools.h"
#include "pgsql.h"
#include "textlines.h"
#include "importdata.h"

//! file type from command line
static std::string sopt_filetype;

//! external prototype for latex.cpp
extern void sp_latex(const std::string& filename, TextLines& lines);

//! external prototype for gnuplot.cpp
extern void sp_gnuplot(const std::string& filename, TextLines& lines);

//! process a stream
static inline TextLines
sp_process_stream(const std::string& filename, std::istream& is)
{
    TextLines lines;

    // read complete file line-wise
    lines.read_stream(is);

    // automatically detect file type
    std::string filetype;

    if (sopt_filetype.size())
    {
        filetype = sopt_filetype;
    }
    else if (is_suffix(filename, ".tex") ||
             is_suffix(filename, ".latex") ||
             is_suffix(filename, ".ltx"))
    {
        filetype = "latex";
    }
    else if (is_suffix(filename, ".gp") ||
             is_suffix(filename, ".gpi") ||
             is_suffix(filename, ".gnu") ||
             is_suffix(filename, ".plt") ||
             is_suffix(filename, ".plot") ||
             is_suffix(filename, ".gnuplot"))
    {
        filetype = "gnuplot";
    }

    // process lines in place
    if (filetype == "latex")
        sp_latex(filename, lines);
    else if (filetype == "gnuplot")
        sp_gnuplot(filename, lines);
    else
        OUT_THROW("--- Error processing " << filename << " : unknown file type, use -f <type>!");

    OUT("--- Finished processing " << filename << " successfully.");

    return lines;
}

//! define identifiers for command line arguments
enum { OPT_HELP, OPT_VERBOSE, OPT_FILETYPE,
       OPT_OUTPUT, OPT_CHECK_OUTPUT, OPT_DATABASE, OPT_RANGE,
       OPT_WORK_DIR };

//! define command line arguments
static CSimpleOpt::SOption sopt_list[] = {
    { OPT_HELP,         "-?", SO_NONE },
    { OPT_HELP,         "-h", SO_NONE },
    { OPT_VERBOSE,      "-v", SO_NONE },
    { OPT_FILETYPE,     "-f", SO_REQ_SEP },
    { OPT_OUTPUT,       "-o", SO_REQ_SEP },
    { OPT_CHECK_OUTPUT, "-C", SO_NONE },
    { OPT_DATABASE,     "-D", SO_REQ_SEP },
    { OPT_RANGE,        "-R", SO_REQ_SEP },
    { OPT_WORK_DIR,     "-W", SO_REQ_SEP },
    SO_END_OF_OPTIONS
};

//! print command line usage
static inline int
sp_process_usage(const std::string& progname)
{
    OUT("Usage: " << progname << " [options] [files...]" << std::endl <<
        std::endl <<
        "Options: " << std::endl <<
        " import      Call IMPORT-DATA subprogram to load SQL tables." << std::endl <<
        "  -v         Increase verbosity." << std::endl <<
        "  -f <type>  Force input file type = latex or gnuplot." << std::endl <<
        "  -o <file>  Output all processed files to this stream." << std::endl <<
        "  -C         Verify that -o output file matches processed data (for tests)." << std::endl <<
        "  -D <type>  Select SQL database type and file or database." << std::endl <<
        "  -R <name>  Process only named RANGE in files." << std::endl <<
        "  -W <dir>   Change working directory at start-up." << std::endl);

    return EXIT_FAILURE;
}

//! process LaTeX or Gnuplot, main function
static inline int
sp_process(int argc, char* argv[])
{
    // output file name
    std::string opt_outputfile;

    // database connection to establish
    std::string opt_db_conninfo;

    // working directory
    std::string opt_work_dir;

    //! parse command line parameters using SimpleOpt
    CSimpleOpt args(argc, argv, sopt_list);

    while (args.Next())
    {
        if (args.LastError() != SO_SUCCESS) {
            OUT(argv[0] << ": invalid command line argument '" << args.OptionText() << "'");
            return EXIT_FAILURE;
        }

        switch (args.OptionId())
        {
        case OPT_HELP: default:
            return sp_process_usage(argv[0]);

        case OPT_VERBOSE:
            gopt_verbose++;
            break;

        case OPT_FILETYPE:
            sopt_filetype = args.OptionArg();
            break;

        case OPT_OUTPUT:
            opt_outputfile = args.OptionArg();
            break;

        case OPT_CHECK_OUTPUT:
            gopt_check_output = true;
            break;

        case OPT_DATABASE:
            opt_db_conninfo = args.OptionArg();
            break;

        case OPT_RANGE:
            gopt_ranges.push_back(args.OptionArg());
            break;

        case OPT_WORK_DIR:
            opt_work_dir = args.OptionArg();
            break;
        }
    }

    if (!opt_work_dir.empty()) {
        if (chdir(opt_work_dir.c_str()) != 0)
            OUT_THROW("Error chdir() to work directory: " << strerror(errno));
    }

    // make connection to the database
    if (!g_db_connect(opt_db_conninfo))
        OUT_THROW("Fatal: could not connect to a SQL database");

    // open output file or string stream
    std::ostream* output = NULL;
    if (gopt_check_output)
    {
        if (!opt_outputfile.size())
            OUT_THROW("Fatal: checking output requires an output filename.");

        output = new std::ostringstream;
    }
    else if (opt_outputfile == "-")
    {
        output = &std::cout;
    }
    else if (opt_outputfile.size())
    {
        output = new std::ofstream(opt_outputfile.c_str());

        if (!output->good())
            OUT_THROW("Error opening output stream: " << strerror(errno));
    }

    // process file commandline arguments
    if (args.FileCount())
    {
        for (int fi = 0; fi < args.FileCount(); ++fi)
        {
            const char* filename = args.File(fi);

            std::ifstream in(filename);
            if (!in.good()) {
                OUT_THROW("Error reading " << filename << ": " << strerror(errno));
            }
            else {
                TextLines out = sp_process_stream(filename, in);

                if (output)  {
                    // write to common output
                    out.write_stream(*output);
                }
                else {
                    // overwrite input file
                    in.close();
                    std::ofstream outfile(filename);
                    if (!outfile.good())
                        OUT_THROW("Error writing " << filename << ": " << strerror(errno));

                    out.write_stream(outfile);
                    if (!outfile.good())
                        OUT_THROW("Error writing " << filename << ": " << strerror(errno));
                }
            }
        }
    }
    else // no file arguments -> process stdin
    {
        OUT("Reading text from stdin ...");
        TextLines out = sp_process_stream("stdin", std::cin);

        if (output)  {
            // write to common output
            out.write_stream(*output);
        }
        else {
            // write to stdout
            out.write_stream(std::cout);
        }
    }

    // verify processed output against file
    if (gopt_check_output)
    {
        std::ifstream in(opt_outputfile.c_str());
        if (!in.good()) {
            OUT("Error reading " << opt_outputfile << ": " << strerror(errno));
            return EXIT_FAILURE;
        }
        std::string checkdata = read_stream(in);

        assert(output);
        std::ostringstream* oss = (std::ostringstream*)output;

        if (checkdata != oss->str())
        {
            OUT("Mismatch to expected output file:");
            simple_diff(oss->str(), checkdata);
            OUT_THROW("Mismatch to expected output file " << opt_outputfile);
        }
    }

    if (output) {
        if (output != &std::cout)
            delete output;
    }

    g_db_free();

    return EXIT_SUCCESS;
}

//! main(), yay.
int main(int argc, char* argv[])
{
    try {
        if (argc >= 2 &&
            (strcmp(argv[1], "import") == 0 ||
             strcmp(argv[1], "import-data") == 0))
        {
            return ImportData().main(argc-1, argv+1);
        }
        else
        {
            return sp_process(argc, argv);
        }
    }
    catch (std::runtime_error& e)
    {
        OUT(e.what());
        return EXIT_FAILURE;
    }
}
