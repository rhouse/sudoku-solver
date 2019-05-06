               A Sudoku solver written in C by Roger House


The following file structure is used:

    sudoku-solver
      problems          Directory of example input puzzles for the solver
      solutions         Directory of solutions to the example puzzles
      c                 Script to compile the solver on GNU/Linux
      c.bat             Batch file to compile the solver on Windows
      license.txt       An MIT license for the sudoku solver
      README            The file you are now reading
      sudoku.c          Source code for the sudoku solver
      sudoku.exe        Executable for Windows 
      t                 Script to solve all puzzles in the problems directory  
      t.bat             Batch file to solve all puzzles on Windows
      test-01.solution  Solution to a test case run by script u
      test-01.windows   Solution to a test case run by script u.bat
      test-02.solution  Solution to a test case run by script u
      test-02.windows   Solution to a test case run by script u.bat
      u                 Script for testing the sudoku executable
      u.bat             Script for testing the sudoku executable on Windows

On a GNU/Linux platform

Compile sudoku.c:

    ./c

Test the executable:

    ./u

If there is no output from the above command, then you have a working 
executable.  Do this:

    ./sudoku

This will display a usage comment explaining the syntax of input files, etc.  

To solve all puzzles in the problems directory, placing their solutions 
in the solutions directory, do this:

    ./t

There are two puzzles (hard-01.txt and hard-02.txt) which take about 20 
seconds each on a 2.83GHz CPU.  Otherwise most puzzles are solved in well 
under a second.  (Two puzzles which are not processed by the t script are 
n-eq-5.txt and n-eq-6.txt.  These are 25x25 and 36x36 puzzles with all 
cells blank.  The solver may very well run for an extremely long time on 
these two problems.)

On a Windows platform

As a first step, try to run the sudoku.exe Windows executable:

    sudoku

If this works, there is no need to run c.bat in the directions just below.

Follow the same instructions as for GNU/Linux, but omit the './' when running 
scripts, and, for good measure, use the '.bat' extension:

    c.bat
    u.bat
    t.bat

Note that u.bat will display output though ./u does not.  In general there 
may be some issues with line terminators on text files.  On GNU/Linux the 
single character LF (linefeed) is used, and on Windows the pair CRLF 
(carriage return, linefeed) is used.  So files which are essentially the 
same may not compare the same due to line terminator differences.

<end README>
