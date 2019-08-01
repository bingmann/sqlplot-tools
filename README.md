## Summary

SqlPlotTools is a tool to **process data series** from algorithm experiments **using SQL statements** and embed the results in **gnuplot** datafiles or **pgfplots** LaTeX files. Using SQL to generate plots can be see as **cracking a nut with a sledgehammer**, but it really works well in practice.

The data series can be read by parsing simple text files for `RESULT` lines, which are **automatically imported into SQL tables**. A `RESULT` line contains one row of data in form of **`key=value pairs`**.

To generate plots, one then writes (arbitrarily complex) **SQL statements embedded in Gnuplot or LaTeX** files as comments. The embedded SQL is then executed by SqlPlotTools against the imported data, and the results are formatted as Gnuplot or Pgfplots pictures. The toolset can also create LaTeX tabular's in a similar way.

There are many advantages of this approach:

* No need for SQL in the **experimental code**, it **just needs to write RESULT text lines**. All extra debug output is ignored during import, thus one can save the complete output of an experimental program run for future reference.

* The **plotted values are embedded** in the LaTeX or Gnuplot code, thus after processing by SqlPlotTools the full data is available and can be made public.

* Furthermore, the **SQL statement completely specifies** how the results are generated from the input. Thus even when complex expressions are needed to generate the plots, they are still available inside the LaTeX file as a comment and can be verified by other authors.

* The declarative power of SQL is available for generating plots. One can use subqueries, JOINs between datasets and more to generate data series.

* One streamlined process to generate plots from experiments. Thus real-time plotting is possible!

The only slight disadvantage is that the SQL statement processing requires **an SQL database**. The current SqlPlotTools version supports SQLite3, PostgreSQL, and MySQL on Linux. I recommend to **start with SQLite3**, because it is embedded in the SqlPlotTools binaries, and no extra database server is needed. For more complex applications, I recommend Postgresql, since it is the most flexible and advanced database.

For more information, see the tutorial below.

## Downloads

There is no official release as yet. So use the git repository and maybe fork it to modify the source for your needs.

Git repository: git clone [https://github.com/bingmann/sqlplottools.git](https://github.com/bingmann/sqlplottools)

Current git build and test suite status: [![Build Status](https://travis-ci.org/bingmann/sqlplot-tools.png?branch=master)](https://travis-ci.org/bingmann/sqlplot-tools)

SqlPlotTools is published under the [GNU General Public License v3 (GPL)](http://opensource.org/licenses/GPL-3.0), which can be found in the file COPYING.

## Compilation

SqlPlotTools is written in C++ and requires the Boost.Regex and SQLite3 development libraries. Furthermore, it can optionally be compiled with PostgreSQL and MySQL support, to function as a client for these database server. Install these libraries using your distribution's package manager.

On Ubuntu or Debian this can be done using:

```
apt-get install libboost-regex1.48-dev libsqlite3-dev libpq-dev libmysqlclient-dev
```

The compilation process uses CMake, and the following sequence of commands will correctly download and compile SqlPlotTools:

```
git clone https://github.com/bingmann/sqlplottools.git
cd sqlplottools
mkdir build; cd build
cmake ..
make
```

The main program is **`sqlplot-tools`**, located in `src/`.

# Tutorial

The SqlPlotTools package contains a very simple C++ example experiment in [examples/sorting-speed](examples/sorting-speed), which measures the speed of sorting integer items using `std::sort`, `std::stable_sort` and STL's heap sort. The snippets in the following tutorial are largely taken from this example.

## Creating RESULT data lines

When performing experiments, usually many data points are generated, and each data point has many parameters. It is often unclear how the final plots are going to be created from the parameter spaces. Likewise, experimental programs are usually already very complex, and adding SQL libraries or similar is out of the question.

With SqlPlotTools one only has to interleave the debug output of the program with `RESULT` lines, which look like this:

```
RESULT algo=std::sort size=268435456 time=29.556 repeats=1 iteration=7 typesize=4 datasize=1073741824
```

In C++, such a line can be generated using code similar to:

```c++
std::cout << "RESULT"
          << " algo=" << algoname
          << " size=" << size
          << " time=" << ts2-ts1
          ...
```

No extra libraries are needed, and all other lines outputted by the program will be ignored during the data import. The data import is very fast, thus even large sets of results can be processed conveniently.

Note: If you need to have spaces in text fields in a RESULT, then you must use tabs as key=value delimiters. If a RESULT line contains any TAB character, then the line is split by tabs instead of spaces. Quoted values are currently not supported.

To see how SqlPlotTools imports data sets, we suggest you run

```
touch test.db
sqlplot-tools import-data -D sqlite:test.db ex1 examples/sorting-speed/stats.txt
```

This will import RESULT rows from the included [stats.txt](examples/sorting-speed/stats.txt) file into the table ex1 in an SQLite3 database called `test.db`. The types of the different columns are automatically detected during import, thus there is no need to specify a `CREATE TABLE` directive. Without the `-D sqlite:test.db`, the table would be created in a temporary in-memory database, and thus discarded after the program ends. However, since we saved the database, we can manually select from the data. The imported table looks as follows:

```sql
sqlite> SELECT * FROM ex1 LIMIT 5;
algo        size        time        repeats     iteration   typesize    datasize
----------  ----------  ----------  ----------  ----------  ----------  ----------
std::sort   1024        0.328101    32768       0           4           4096
std::sort   1024        0.316489    32768       1           4           4096
std::sort   1024        0.316419    32768       2           4           4096
std::sort   1024        0.315962    32768       3           4           4096
std::sort   1024        0.316174    32768       4           4           4096
```

## Generating GnuPlots using SQL Statements

Automatically importing data is already pretty neat, but now we will generate a Gnuplot from the data.

Regard the file [examples/sorting-speed/speed.plot](examples/sorting-speed/speed.plot). This file contains boilerplate Gnuplot code, except for two comments. These two comments are directives, which are processed by SqlPlotTools (other comments are ignored). Plot directives have to be in CAPITALS!

The first comment is equivalent to calling the program with `import-data`:

```
# IMPORT-DATA stats stats.txt
```

This line imports [stats.txt](examples/sorting-speed/stats.txt) into the table `stats`, except that the data imported into a *temporary table*! To make tables permanent add `-P` as a "command line" parameter (`IMPORT-DATA -P stats stats.txt`).

The second comment is a multi-line SqlPlotTools directive, due to the double `#` at the beginning of each line:

```
## MULTIPLOT(algo) SELECT LOG(2,size) AS x, AVG(time / repeats / size * 1e9) AS y, MULTIPLOT
## FROM stats GROUP BY MULTIPLOT,x ORDER BY MULTIPLOT,x
```

This is already a pretty complex SQL statement, and the `MULTIPLOT` directive specifies that a plot with **multiple lines** will be generated from it. To simplify writing the SQL statement, all occurrences of "MULTIPLOT" is replaced by the content in the parenthesis, in the example with "algo".  We first focus on the result of the SQL statement:

```sql
sqlite> SELECT LOG(2,size) AS x, AVG(time / repeats / size * 1e9) AS y, algo FROM stats GROUP BY algo,x ORDER BY algo,x;
x           y                algo
----------  ---------------  ----------------
10.0        52.655021349589  std::heap_sort
11.0        54.477930068969  std::heap_sort
[...]
30.0        576.41124973694  std::heap_sort
10.0        9.5095654328664  std::sort
11.0        10.231421391169  std::sort
12.0        10.804375012715  std::sort
[...]
30.0        117.53315726916  std::sort
10.0        17.541384696960  std::stable_sort
11.0        18.972245852152  std::stable_sort
[...]
30.0        110.23053278525  std::stable_sort
```

The SQL statement already suggests how the data rows are transformed by SqlPlotTools into plot lines. The parenthesised argument "algo" (in general "col1,col2,col3") is used to **group multiple rows** into a plot line. The plot line is automatically labelled using the values of the group columns.

To generate the plot data from the stats and update the Gnuplot file, simply run `sqlplot-tools` in the `examples/sorting-speed` directory, followed by `gnuplot`:

```
sqlplot-tools speed.plot
gnuplot speed.plot
```

The `sqlplot-tools` call will parse [speed.plot](examples/sorting-speed/speed.plot) for SQL directives, execute them, and modified the plot file. The lines after these directives are **replaced** with the corresponding results, and in the case of Gnuplot, and additional [speed-data.txt](examples/sorting-speed/speed-data.txt) file is generated, which contains the actual data points of the plot. Since the current tarball already contains `speed-data.txt` and `speed.pdf`, we suggest deleting these two files and recreating them with the command above.

## Generating LaTeX Pgfplots and Tabulars using SQL Statements

Generating Gnuplots is fast, however, for publications in LaTeX the [Pgfplots](http://pgfplots.sourceforge.net/) package renders plots much nicer using [TikZ](http://pgf.sourceforge.net/), all natively in LaTeX. The main problem of Pgfplots is to get the data into the plot. And this what SqlPlotTools was originally intended to do: to generate high-quality Pgfplots directly from data.

The SqlPlotTools package contains an [example LaTeX file](examples/sorting-speed/paper.tex) and the [generated PDF](examples/sorting-speed/paper.pdf), again plotting data from the sorting speed experiment.

While Pgfplots does have facilities to read datafiles like Gnuplot, the more general workflow is to **embedd the data points** directly in LaTeX. And this is what SqlPlotTools does: it executes SQL statements against the datasets and **replaces** the lines after the directive with corresponding Pgfplots lines. While replacing the lines, it tries hard to just replace the coordinates clause, and leaving additional formatting in tact.

In the example [paper.tex](examples/sorting-speed/paper.tex) you find the following pgfplot, in which the `\\addplot` lines were generated by SqlPlotTools:

```latex
\begin{tikzpicture}
  \begin{axis}[
    title={Simple C++ Sorting Test},
    xlabel={Item Count [$\log_2(n)$]},
    ylabel={Run Time per Item [Nanoseconds / Item]},
    ]

    %% MULTIPLOT(algo) SELECT LOG(2, size) AS x, MEDIAN(time / repeats / size * 1e9) AS y, MULTIPLOT
    %% FROM stats GROUP BY MULTIPLOT,x ORDER BY MULTIPLOT,x
    \addplot coordinates { (10,52.6541) (11,54.4712) (12,55.6132) (13,57.2541) (14,60.2207) (15,62.5536) (16,63.3866) (17,64.8955) (18,68.0408) (19,71.5148) (20,74.3139) (21,80.9988) (22,92.9332) (23,119.146) (24,177.475) (25,310.999) (26,348.729) (27,395.465) (28,448.193) (29,509.227) (30,576.299) };
    \addlegendentry{algo=std::heap\_sort};
    \addplot coordinates { (10,9.53844) (11,10.2281) (12,10.8056) (13,11.73) (14,12.5076) (15,13.1773) (16,13.9202) (17,14.7259) (18,15.7581) (19,16.9725) (20,18.7205) (21,21.719) (22,27.0947) (23,37.3578) (24,58.0028) (25,100.04) (26,103.915) (27,106.265) (28,110.105) (29,113.754) (30,117.47) };
    \addlegendentry{algo=std::sort};
    \addplot coordinates { (10,17.5403) (11,18.9741) (12,20.8212) (13,22.5908) (14,25.2842) (15,27.2218) (16,29.3958) (17,30.9679) (18,33.3819) (19,35.364) (20,40.7699) (21,44.7148) (22,49.4075) (23,56.5833) (24,69.603) (25,92.3091) (26,96.3424) (27,99.2067) (28,103.255) (29,106.148) (30,110.22) };
    \addlegendentry{algo=std::stable\_sort};

  \end{axis}
\end{tikzpicture}
```

Note that the MULTIPLOT directive is **identical** to the one in the Gnuplot example, except for being written as LaTeX comments.  This example creates a high-quality plot, within LaTeX, using matching fonts and sizes. For more information on how to format the plot, see the [Pgfplots manual](http://pgfplots.sourceforge.net/pgfplots.pdf).

Additionally to generating plots, SqlPlotTools can also generate `tabular` data. The [paper.tex](examples/sorting-speed/paper.tex) contains a reasonably complex example of such tabular. In general, SqlPlotTools will just output the result of an SQL query as a row/column tabular, just like the query is defined. Column headers are ignored, and you must provide the tabular itself including column numbers and formatting.

```latex
\begin{tabular}{l|rrr}
$n$ & \texttt{std::sort} & \texttt{std::stable\_sort} & STL heap sort \\ \hline
%% TABULAR REFORMAT(col 1-3=(precision=1) row 0-100=(min=bold))
%% SELECT '$2^{' || FLOOR(LOG(2, size)) || '}$' AS x,
%% (SELECT MEDIAN(time / repeats / size * 1e9) FROM stats s1 WHERE s1.algo='std::sort' AND s1.size = s.size GROUP BY s1.size),
%% (SELECT MEDIAN(time / repeats / size * 1e9) FROM stats s1 WHERE s1.algo='std::stable_sort' AND s1.size = s.size GROUP BY s1.size),
%% (SELECT MEDIAN(time / repeats / size * 1e9) FROM stats s1 WHERE s1.algo='std::heap_sort' AND s1.size = s.size GROUP BY s1.size)
%% FROM stats s
%% GROUP BY s.size ORDER BY s.size
$2^{10}$ &  \bf 9.5 &      17.5 &  52.7 \\
$2^{11}$ & \bf 10.2 &      19.0 &  54.5 \\
$2^{12}$ & \bf 10.8 &      20.8 &  55.6 \\
[...]
% END TABULAR SELECT '$2^{' || FLOOR(LOG(2, size)) || '}$' AS x, (SELECT MEDI...
\end{tabular}
```

The included example is already pretty complex: it first selects all the sizes tested by the experiment, and then selects the median of the test result for the three algorithms using a subquery. Formatting the results of `TABULAR` can be done in two ways: one can either use the SQL database facilities (e.g. by defining arbitrary formatting functions using SQL procedures), or the experimental `REFORMAT()` subclause of `TABULAR`. As `REFORMAT` is not finished, please looks into the `reformat.cpp` source file for available directives.

Another additional feature of SqlPlotTools in LaTeX is to generate plain text result tables. The main application of this is to calculate summary values, which are then contained inside the text. For example: "the total runtime of all experiments in this paper is 12345 seconds". To actually calculate the "12345", we again use an SQL statement (probably involving a `SUM()`). SqlPlotTools allows one to use a `TEXTTABLE` command, which outputs a formatted SQL result similar to what an SQL command line tool would output. This text table is embedded inside the LaTeX file, and usually wrapped inside a comment area.

```latex
\begin{comment}
% TEXTTABLE SELECT COUNT(*), SUM(time) FROM stats
+-------+--------------+
| count |          sum |
+-------+--------------+
|   945 | 24504.381188 |
+-------+--------------+
% END TEXTTABLE SELECT COUNT(*), SUM(time) FROM stats
\end{comment}
```

Thus the authors of the paper (others, but including yourself) can verify **where the numbers in the text are coming from**.

## Exits

Written 2014-05-17 by Timo Bingmann
