// suduko.c  --  Solve suduko puzzles
//
// Written by:          Roger House
// Original version:    2006 Feb 19
// Revision:            2007 Jun 25 -- Allow larger than 9x9 boards
//                      2011 Nov 05 -- Clean up and refactor
//
// Copyright (C) 2006-2011 by Roger House.  See license.txt.
//
// See display_usage for a description of the input file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdint.h>

#ifndef _MSC_VER
    #include <stdbool.h>  
#else
    // Microsoft can't be bothered with standards
    #define bool unsigned short
    #define false 0
    #define true  1
#endif

#ifndef _MSC_VER
    #include <inttypes.h>
#else
    // Microsoft can't be bothered with standards
    #define PRIu64 I64u
#endif

/*
                      How a Sudoku puzzle is solved

A Sudoku puzzle is represented as a 9x9 array of squares, each of which has 
a current value, a frozen flag, and information about neighbors and candidate
values.  (Note:  The puzzle can be 16x16, 25x25, or 36x36, but we use the 
standard 9x9 for this discusion.)  Here are more details about the info stored 
for a square:

  Current value
    The current value of a square is the value currently occupying the square, 
    e.g., 1, 2, ..., 9.  A zero current value means the square is blank.  

  Frozen flag
    Each square which is nonblank in the original problem statement is frozen, 
    i.e., the current value for the square cannot be changed.  Other squares 
    may also be frozen, e.g., those squares filled in by various optimizations 
    which determine that a particular square is forced to have a particular 
    value.

  Neighbors
    Each square s has a set of neighbors, namely those squares which are in 
    the same row, the same column, or the same subsquare as s.  Neighbors are 
    important because if some neighbor of s has current value v, then it is 
    not possible for s to have value v.

  Candidates
    At any given moment, there is a set of candidate values which an unfrozen 
    square might have.  This info is stored in a bit mask, a 1-bit in position 
    k indicating that value k is a candidate for the value of the square.  A 
    stack is used to save previous bit masks as the algorithm tries different
    values.  (Future:  Look into getting rid of the candidate stack.)

After the initial setup of a puzzle is loaded from the input file, all info 
for each square is set up.  The list of candidates for a square is determined
by looking to see which values do not appear in neighbor squares. 

The next step in solving a puzzle is checking to see if certain squares are 
forced to have certain values.  For example if a square has exactly one 
candidate value, then that candidate is assigned to the square and the square 
is frozen.

There are other situations in which a value is forced.  For example, say 
square s has candidate value v, and no other square in the same row as s has 
candidate value v.  Then v must be the value to assign to square s.  Similar 
situations arise for columns and subsquares.

Once all forced values have been determined and placed and the corresponding 
squares frozen, the algorithm proper begins.  The algorithm is rather simple:
Scan all the squares of the puzzle, and for each unfrozen square, pick the 
first untried candidate value and assign it to the square.  Stack the square. 
If an unfrozen square has no untried candidates, unstack the last move and 
proceed from there.  This is more or less a standard backtracking algorithm.
See the comment for find_solution below for more details.
*/

/*
    MAX_N is the maximum subsquare size allowed.  It is possible to increase
    MAX_N to 8, but no larger since uint64_t is used to store bit masks.  
    However when MAX_N = 8, the board is 64 on a side, which means 4096 
    squares, which means about 24MB is used by board, saved_board, etc.  With 
    MAX_N = 6, only a bit more than 4MB is used.  It might be a good idea to
    use malloc and free instead of fixed-size arrays.  At present most arrays
    are of size MAX_N2+1 with element zero not used.  Elements 1, 2, ..., 
    MAX_N2 are used for the suduko alphabet.
*/

#define MAX_N 6
#define MAX_N2 (MAX_N*MAX_N)
#define MAX_NUM_SQUARES (MAX_N2*MAX_N2)
#define MAX_NUM_NEIGHBORS (3*MAX_N2 - 2*MAX_N - 1)

// When a board larger than 9x9 is used these alphabets are used:
//
//  N   N^2   alphabet
//  ---------------------------------
//  3    9    1-9
//  4   16    1-9, 0, A-F
//  5   25    1-9, 0, A-O
//  6   36    1-9, 0, A-Z
//  7   49    1-9, 0, A-Z, a-m
//  8   64    1-9, 0, A-Z, a-z, #, $


const bool allowNinFile = true ;    // Allow '//N=d' in input file

typedef uint64_t Bitmask;

typedef struct  // Square description
{
    int     cur_value ;                 // Current value of square (0 is empty)
    bool    frozen ;                    // True means cur_value cannot change

    int     neighbor_i[MAX_NUM_NEIGHBORS];  // row-coordinate of neighbors
    int     neighbor_j[MAX_NUM_NEIGHBORS];  // col-coordinate of neighbors

    Bitmask candidate [MAX_NUM_NEIGHBORS];  // stack of candidates
    int     topmask;                        // index to top of candidate stack
    int     num_candidates;                 // no. candidates in candidate[0] 
                                            //   bitmask
} Square;

/*
 A candidate bitmask has a 1-bit set for each piece which could be placed 
 on the square.  Frozen squares have no candidates.  Initially, topmask == 0
 and candidate[0] contains a bitmask showing all possible candidate values 
 for the square.   num_candidates contains the number of 1-bits in 
 candidate[0]; this value does not change.  As moves are tried, 1-bits are 
 set to 0-bits, and the changed bitmasks are stacked. 
*/


typedef struct  // game statistics
{
    int      num_occupied_squares_originally;
    uint64_t sum_num_candidates_originally;

    int      num_frozen_as_only_candidate;
    int      num_frozen_by_row_optimization;
    int      num_frozen_by_column_optimization;
    int      num_frozen_by_subsquare_optimization;

    uint64_t sum_num_candidates_after_optimization;

    int      num_unstackings;

} Game_Stats;


typedef struct  // Game description
{
    int N;                  // Side of a subsquare (= 3 for a 9x9 board)
    int N2;                 // N squared, the side of the board
    int num_squares;        // Total no. of squares on the board
    int num_neighbors;      // No. of neighbors each square has (see below)

    Square board       [MAX_N2+1] [MAX_N2+1];
    Square saved_board [MAX_N2+1] [MAX_N2+1];

    Game_Stats stats;

} Game;


// A global Game is used to avoid using up large amounts of stack space:

Game game;


// Function prototypes

Bitmask bit ( int i ) ;
bool check_for_only_one_candidate ( Game *G ) ;
void compute_candidate_stats ( Game *G, bool before);
void convert_bitmask ( Bitmask b, char *s ) ;
void display_usage ( void ) ;
bool find_candidate ( Game *G, int row, int col ) ;
bool find_candidates_x ( Game *G ) ;
bool find_candidates ( Game *G ) ;
void find_solution ( Game *G ) ;
void find_sub_square ( Game *G, int row, int col, int *srow, int *scol ) ;
void freeze_one ( Game *G, int row, int col, int piece ) ;
int  get_first_candidate ( Game *G, Bitmask b ) ;
void get_line (
        Game *G,
        bool allowN,
        char *linebuff,
        FILE *infp,
        char *file_name);
void init_bit_mask_array ( Bitmask b[] ) ;
void init_board ( Game *G, int n ) ;
void load_file ( Game *G, char *file_name, bool allowN );
int  map_alphabet_to_int ( char c ) ;
int  map_int_to_alphabet ( Game *G, int i ) ;
bool preprocess_board ( Game *G ) ;
void print_bit_mask_array ( Bitmask b[]  ) ;
void print_board ( Game *G, bool everything ) ;
void print_candidates ( Game *G  ) ;
void print_statistics ( Game *G ) ;
void test_sub_square ( Game *G ) ;
void save_board ( Game *G ) ;
bool try_column_optimization ( Game *G ) ;
bool try_row_optimization ( Game *G ) ;
bool try_subsquare_optimization ( Game *G ) ;
void update_neighbors (
        Game *G,
        int row,
        int col,
        int piece_number,
        bool make_avail ) ;
bool verify_solution ( Game *G, bool full_solution ) ;
bool verify_solution_to_our_problem ( Game *G ) ;


int main ( int argc, char *argv[] )
{
    assert ( MAX_N <= 8 );

//  test_sub_square ( &game ) ;

    if ( argc != 2 )
    {
        display_usage();
        exit(1);
    }

    init_board ( &game, 3 );    // Default is 3x3 subsquare (9x9 board)

    load_file ( &game, argv[1], allowNinFile );

    print_board ( &game, false );
 
    save_board ( &game );

    if ( ! verify_solution ( &game, false ) )
    {
        printf("\n****** INVALID SETUP *****\n");
        return 0 ;
    }

    if ( ! preprocess_board ( &game ) )
    {
        printf("\n****** NO SOLUTIONS EXISTS *****\n");
        return 0 ;
    }

    find_solution ( &game ) ;

    printf("\n\n");

    print_board ( &game, false ); 

    if ( ! verify_solution_to_our_problem ( &game ) )
        printf("\n****** NOT A SOLUTION TO THE ORIGINAL PROBLEM *****\n");
    else 
        if ( ! verify_solution ( &game , true ) )
        printf("\n****** INVALID SOLUTION *****\n");

    print_statistics ( &game ) ;

    return 0;

}   // end main


void display_usage ( void )
{
#define p(x) fprintf ( stderr, x )

p("\n");
p("Usage:  suduku  input-file\n");
p("\n");
p("where the input file has the form in the example shown below.  The \n");
p("solution of the problem is written to stdout, along with statistics\n");
p("about the problem and its solution.  Here is an example input file:\n");
p("\n");
p("// From The Santa Rosa, CA Press Democrat, 2006 Feb 18.\n");
p("\n");
p("  - - 4   5 - -   - - 9\n");
p("  - 8 -   - 7 2   - - -\n");
p("  2 - 1   9 - -   - 7 -\n");
p("                      \n");
p("  - - -   2 5 -   9 - -\n");
p("  - 1 9   - - -   6 5 -\n");
p("  - - 2   - 9 7   - - -\n");
p("                       \n");
p("  - 6 -   - - 9   2 - 4 \n");
p("  - - -   3 6 -   - 9 - \n");
p("  1 - -   - - 8   3 - - \n");
p("\n");
p("Note that blank lines, lines consisting solely of whitespace, and lines\n");
p("beginning with '//' are ignored, with one exception:  A line before the\n");
p("first line of the problem which begins with '//N=<digit>' specifies \n");
p("that the problem does not have the default 3x3 subsquare size, but has \n");
p("a size specified by the digit (which must be one of 3, 4, 5, or 6). \n");
p("Thus problems of sizes, 9x9, 16x16, 25x25, and 36x36 can be solved. \n");
p("For the larger problems these alphabets are used:\n");
p("\n");
p("    N   N^2    alphabet  \n");
p("    ---------------------\n");
p("    3    9    1-9        \n");
p("    4   16    1-9, 0, A-F\n");
p("    5   25    1-9, 0, A-O\n");
p("    6   36    1-9, 0, A-Z\n");
p("\n");
p("Note that whitespace is only required between adjacent digits.\n");

#undef p

}   // end display_usage


void init_board ( Game *G, int n )
/*
    Initialize game G with subsquare size n x n.
*/
{
    Square  *p;
    int     neighbor_count;
    int     srow, scol;
    int     i, j, k;
    int     ii, jj;

    G->N  = n;
    G->N2 = n * n;
    G->num_squares =  G->N2 * G->N2 ;
    G->num_neighbors = 3*G->N2 - 2*G->N - 1 ;

    // Note that all board elements are initialized, even the 
    // elements indexed by zero, which are not used.

    for ( i = 0 ;  i <= G->N2 ;  ++i)
    {
        for ( j = 0 ;  j <= G->N2 ;  ++j)
        {
            p = &G->board[i][j] ;
            p->cur_value = 0 ;
            p->frozen = false ;

            // Set up candidates

            for ( k = 0 ;  k < G->num_neighbors ;  ++k )
                p->candidate[k] = 0 ;
            p->topmask = -1 ;
            p->num_candidates = 0 ;

            // Set up neighbors

            if ( i == 0 || j == 0 )
            {
                for ( k = 0 ;  k < G->num_neighbors ;  ++k )
                    p->neighbor_i[k] = p->neighbor_j[k] = -1 ;
                continue ;
            }

            // neighbors in same column as square (i, j)

            neighbor_count = 0 ;

            for ( ii = 1 ;  ii <= G->N2 ;  ++ii)
            {
                if ( ii == i )
                    continue ;
                p->neighbor_i[neighbor_count] = ii ;
                p->neighbor_j[neighbor_count] = j  ;
                ++neighbor_count;
            }

            // neighbors in same row as square (i, j)

            for ( jj = 1 ;  jj <= G->N2 ;  ++jj)
            {
                if ( jj == j )
                    continue ;
                p->neighbor_i[neighbor_count] = i  ;
                p->neighbor_j[neighbor_count] = jj ;
                ++neighbor_count;
            }

            // neighbors in same subsquare as square (i, j)

            find_sub_square ( G, i, j, &srow, &scol ) ;

            for ( ii = srow ;  ii < srow + G->N ;  ++ii )
            {
                if ( ii == i )
                    continue ;

                for ( jj = scol ;  jj < scol + G->N ;  ++jj )
                {
                    if ( jj == j )
                        continue ;

                    p->neighbor_i[neighbor_count] = ii ;
                    p->neighbor_j[neighbor_count] = jj ;
                    ++neighbor_count;
                }
            }


        }   //  end j-loop over columns

    }   //  end i-loop over rows

    assert ( neighbor_count == G->num_neighbors );

    // Zero out all the statistics

    memset ( &G->stats , 0 , sizeof(Game_Stats) ) ;

}   //  end init_board


void save_board ( Game *G )
/*
    Save the board for a later check that the final solution is indeed 
    a solution to the original problem.
*/
{
    memcpy ( G->saved_board , G->board , sizeof(G->board) ) ;
}


void load_file ( Game *G, char *file_name, bool allowN )
/*
    The file specified by file_name is opened and loaded into game G.  If 
    allowN is true, then an input line containing '//N=d' for d an integer 
    is checked for.  The default is d=3, specifying a 3x3 subsquare (i.e., 
    a 9x9 board).
*/
{
#define LINESIZE 500
    char    linebuff [ LINESIZE ] ;
    FILE    *infp;
    char    *c;
    char    d;
    Square  *p;
    int     i, j, k ;

    infp = fopen ( file_name, "rt");

    if ( infp == NULL )
    {
        fprintf ( stderr, "Can't open %s\n", file_name );
        exit(1);
    }

    for ( i = 1 ;  i <= G->N2 ;  ++i )  // Loop for rows
    {
        get_line ( G, allowN, linebuff, infp, file_name ) ;
        allowN = false ;    // Can't change N after first line
        c = linebuff;

        for ( j = 1 ;  j <= G->N2 ;  ++j )  // Loop over columns
        {
            p = &G->board[i][j] ; 

            while ( isspace(*c) )
                ++c;

            if ( *c == '-' )
            {
                p->cur_value = 0 ;
                p->frozen = false ;
            }
            else
            {
                d = map_alphabet_to_int ( *c ) ;
                if ( d == -1 || d > G->N2 )
                {
                    fprintf ( stderr, "In file %s square (%d, %d) is not '-' "
                        "nor a valid character for a puzzle of size %dx%d",
                         file_name, i, j, G->N2, G->N2 ) ;
                    exit(1);
                }
                p->cur_value = d ;
                p->frozen = true ;
                ++G->stats.num_occupied_squares_originally;
            }

            for ( k = 0 ;  k < G->num_neighbors ;  ++k )
                p->candidate[k] = 0 ;
            p->topmask = -1 ;

            ++c ;

        }   // end j-loop over columns

    }   //  end i-loop over rows

}   // end load_file


void get_line (
    Game *G,
    bool allowN,
    char *linebuff,
    FILE *infp,
    char *file_name)
/*
    Get one line of the puzzle for game G from the file specified by infp
    (named file_name - used for error messages only) and store the line in
    linebuff.  If allowN is true, test for a line '//N=d' and process it.
*/
{
    static int linenum = 0 ;

    int n ;

    while ( true )
    {
        ++linenum;

        if ( fgets ( linebuff, LINESIZE, infp ) == NULL )
        {
            fprintf ( stderr, "File %s ended prematurely trying to read "
                                        "line %d\n", file_name, linenum );
            exit(1);
        }

        if ( strncmp ( linebuff, "//", 2 ) == 0 )   // Comment line
        {
            if ( ! allowN )
                continue;
            if ( strncmp ( linebuff+2, "N=", 2 ) != 0 )
                continue;
            n = atoi ( linebuff+4 ) ;
            if ( 3 <= n && n <= MAX_N )
            {
                init_board ( G, n ) ;
                continue;
            }
            fprintf ( stderr, "In file %s a line begins with '//N=' but "
                "an integer in the range [3, %d] does not follow\n", 
                file_name, MAX_N ) ;
            exit(1);
        }

        n = strlen ( linebuff ) ;   // Trim trailing whitespace

        while ( n > 0 && isspace (linebuff [n-1] ) )
        {
            linebuff [n-1 ] = '\0' ;
            --n ;
        }

        if ( n == 0 )       // Skip whitespace lines
            continue;

        break;              // Got a line to process
    }

}   //  end get_line


int map_alphabet_to_int ( char c )
/*
    Map the input character to an integer:

        in  out
        -----------
         1    1
         2    2
        ... ...
         9    9
         0   10
         A   11
         B   12
        ... ...
         Z   36
         a   37
         b   38
        ... ...
         z   62
         #   63
         $   64

    A return value of -1 means the input character is none of the above.

    This function is the inverse of map_int_to_alphabet below.
*/
{
    if ( '1' <= c && c <= '9' )  return c - '1' +  1 ;
    if ( '0' == c             )  return 10 ;
    if ( 'A' <= c && c <= 'Z' )  return c - 'A' + 11 ;
    if ( 'a' <= c && c <= 'z' )  return c - 'a' + 37 ;
    if ( '#' == c             )  return 63 ;
    if ( '$' == c             )  return 64 ;

    return -1 ;

}   //  end map_alphabet_to_int


int map_int_to_alphabet ( Game *G, int i )
/*
    Map the input integer i to a Sudoku character, e.g., '1', '2', '9'.

    This function is the inverse of map_alphabet_to_int above.
*/
{
    static char mapToInt[66] =
        "-"
        "1234567890"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "#$";

    assert ( 0 <= i && i <= G->N2 );

    return mapToInt[i];
}

bool verify_solution ( Game *G, bool full_solution )
/*
    Verify that the board contains a full solution or a partial solution 
    of a Sudoku game, depending on the full_solution input.

    There is a full solution if and only if each row, each column, and each
    subsquare contains each of the values 1, 2, ..., N2 exactly once in any 
    order.

    There is a partial solution if and only if each row, each column, and each
    subsquare contains the values 1, 2, ..., N2 at most once.  

    Return true if the solution is valid; return false otherwise.
*/
{
    int     used[MAX_N2+1];
    Square  *p;
    int     i, j, k, ii, jj;
    int     n ;

    // verify that each row contains 1, 2, ..., N2 in some order

    for ( i = 1 ;  i <= G->N2 ;  ++i)
    {
        for ( k = 0 ;  k <= G->N2 ;  ++k )
            used[k] = 0 ;

        for ( j = 1 ;  j <= G->N2 ;  ++j )
        {
            p = &G->board[i][j] ;
            n = p->cur_value ;
            assert ( 0 <= n && n <= G->N2 ) ;
            ++used[n] ;
        }

        for ( k = 1 ;  k <= G->N2 ;  ++k )
        {
            if ( used[k] > 1 )
            {
                fprintf ( stderr, "Row %d contains %c %d "
                    "times\n", i, map_int_to_alphabet(G, k), 
                    used[k] );
                return false ;
            }
            if ( full_solution && used[k] != 1 )
            {
                fprintf ( stderr, "Row %d contains %c %d "
                    "times\n", i, map_int_to_alphabet(G, k), 
                    used[k] );
                return false ;
            }
        }
    }

    // verify that each column contains 1, 2, ..., N2 in some order

    for ( i = 1 ;  i <= G->N2 ;  ++i)
    {
        for ( k = 0 ;  k <= G->N2 ;  ++k )
            used[k] = 0 ;

        for ( j = 1 ;  j <= G->N2 ;  ++j )
        {
            p = &G->board[j][i] ;
            n = p->cur_value ;
            assert ( 0 <= n && n <= G->N2 ) ;
            ++used[n] ;
        }

        for ( k = 1 ;  k <= G->N2 ;  ++k )
        {
            if ( used[k] > 1 )
            {
                fprintf ( stderr, "Column %d contains %c %d "
                    "times\n", i, map_int_to_alphabet(G, k), 
                    used[k] );
                return false ;
            }
            if ( full_solution && used[k] != 1 )
            {
                fprintf ( stderr, "Column %d contains %c %d "
                    "times\n", i, map_int_to_alphabet(G, k), 
                    used[k] );
                return false ;
            }
        }
    }

    // verify that each subsquare contains 1, 2, ..., N2 in some order

    for ( i = 1 ;  i <= G->N2 ;  i+=G->N )
    {
        for ( j = 1 ;  j <= G->N2 ;  j+=G->N )
        {
            for ( k = 0 ;  k <= G->N2 ;  ++k )
                used[k] = 0 ;

            for ( ii = 0 ;  ii < G->N ;  ++ii )
            {
                for ( jj = 0 ;  jj < G->N ;  ++jj )
                {
                    p = &G->board[i+ii][j+jj] ;
                    n = p->cur_value ;
                    assert ( 0 <= n && n <= G->N2 ) ;
                    ++used[n] ;
                }
            }

            for ( k = 1 ;  k <= G->N2 ;  ++k )
            {
                if ( used[k] > 1 )
                {
                    fprintf ( stderr, "Subsquare (%d, "
                        "%d) contains %c %d times\n", 
                        i, j, map_int_to_alphabet(G, k), 
                        used[k] );
                    return false ;
                }
                if ( full_solution && used[k] != 1 )
                {
                    fprintf ( stderr, "Subsquare (%d, "
                        "%d) contains %c %d times\n", 
                        i, j, map_int_to_alphabet(G, k), 
                        used[k] );
                    return false ;
                }
            }
        }
    }

    return true ;

}   //  end verify_solution


bool verify_solution_to_our_problem ( Game *G )
/*
    Verify that the solution on the board agrees with the saved board.  In
    other words, the saved board positions should be a subset of the current
    board positions.

    Return true if the solution is valid; return false otherwise.
*/
{
    Square  *p, *q;
    int     i, j;

    for ( i = 0 ;  i <= G->N2 ;  ++i)
    {
        for ( j = 0 ;  j <= G->N2 ;  ++j )
        {
            p = &G->board[i][j] ;
            q = &G->saved_board[i][j] ;
            if ( q->cur_value != 0 && q->cur_value != p->cur_value )
                return false;
        }
    }

    return true;

}   //  end verify_solution_to_our_problem


bool preprocess_board ( Game *G )
/*
    Preprocess the board as follows:

        For each square, find all possible candidates for occupying the square
        For those squares with only one possible candidate, place that 
            candidate and freeze the square
        Scan each row to see if there is a candidate which only appears once 
            in the candidate lists for the row; if so, place that candidate 
            and freeze the square
        Scan each column to see if there is a candidate which only appears 
            once in the candidate lists for the column; if so, place that 
            candidate and freeze the square
        Scan each subsquare to see if there is a candidate which only appears 
            once in the candidate lists for the subsquare; if so, place that 
            candidate and freeze the square

    Each time a candidate is placed and a square is frozen, the steps above 
    are repeated, including finding all possible candidates, because the 
    freezing of a square, changes the possible candidates for other squares.

    Board statistics are computed both before and after the preprocessing.

    A true return value means everything is fine.  A false return value means 
    there is at least one empty square which has no candidates.  This is no 
    good because it will be impossible to place anything in this square (there 
    are no candidates to place there).
*/
{
    bool    first_time = true ;
    bool    no_candidates ;

    while ( true )
    {
        no_candidates = ! find_candidates ( G ) ;

        if ( first_time ) 
        {
            compute_candidate_stats ( G, true ) ;
            first_time = false ;
//          break;  Uncomment this line to omit all optimizations
        }

        if ( no_candidates )
            break ;

        if ( check_for_only_one_candidate ( G ) )  continue ;
        if ( try_row_optimization         ( G ) )  continue ;
        if ( try_column_optimization      ( G ) )  continue ;
        if ( try_subsquare_optimization   ( G ) )  continue ;

        break ;
    }

    compute_candidate_stats ( G, false );

    if ( no_candidates )
    {
        print_board ( G, false ) ;
        printf ( "Can't get started:  At least one square "
                        "has no candidates\n" );
        return false ;
    }

    return true ;

}   //  end preprocess_board


bool check_for_only_one_candidate ( Game *G )
/*
    Check for unfrozen squares which have exactly one candidate; 
    the choice of piece is forced to be that one candidate, so 
    place the piece and freeze the square.

    A true return value means that at least one square was frozen 
    by this function.  A false return value means that every unfrozen
    square has more than one candidate.
*/
{
    Square  *p;
    bool    found_one ;
    int     i, j, t;

    found_one = false ;

    for ( i = 1 ;  i <= G->N2 ;  ++i)
    {
        for ( j = 1 ;  j <= G->N2 ;  ++j)
        {
            p = &G->board[i][j] ;
            if ( (!p->frozen) && p->num_candidates == 1 )
            {
                t = get_first_candidate ( G, p->candidate[0] ) ;
                freeze_one ( G, i, j , t );
//printf("square (%d,%d) has exactly one candidate:  %d\n", i, j, t );
                ++G->stats.num_frozen_as_only_candidate;
                found_one = true ;
            }
        }
    }

    return found_one ;

}   //  end check_for_only_one_candidate 


bool try_row_optimization ( Game *G )
/*
    Check each row to see if there is a piece which appears only once 
    as a candidate for the row.  If so, then that piece is frozen to 
    the square for which it is a candidate.

    A true return value means that at least one square was frozen 
    by this function.  A false return value means that this function 
    made no changes to the board.
*/
{
    int     seen[MAX_N2+1];
    int     index[MAX_N2+1];
    Square  *p;
    int     i, j, k;

    seen[0]  = 0;   // not used
    index[0] = 0;   // not used

    for ( i = 1 ;  i <= G->N2 ;  ++i)
    {
        for ( k = 1 ;  k <= G->N2 ;  ++k )
            seen[k] = index[k] = 0;

        for ( j = 1 ;  j <= G->N2 ;  ++j)
        {
            p = &G->board[i][j] ;
            if ( p->frozen )
                continue;
            for ( k = 1 ;  k <= G->N2 ;  ++k )
            {
                if ( p->candidate[0] & bit(k) )
                {
                    ++seen[k] ;
                    index[k] = j ;
                }
            }
        }

        for ( k = 1 ;  k <= G->N2 ;  ++k )
        {
            if ( seen[k] == 1 )
            {
                freeze_one ( G, i, index[k] , k ) ;
//printf("square (%d,%d) frozen to piece %d by row optimization\n", i, 
//                                                           index[k] , k);
                ++G->stats.num_frozen_by_row_optimization;
                return true ;
            }
        }

    }   //  end i-loop over rows

    return false ;

}   //  end try_row_optimization


bool try_column_optimization ( Game *G )
/*
    Check each column to see if there is a piece which appears only once 
    as a candidate for the column.  If so, then that piece is frozen to 
    the square for which it is a candidate.

    A true return value means that at least one square was frozen 
    by this function.  A false return value means that this function 
    made no changes to the board.
*/
{
    int     seen[MAX_N2+1];
    int     index[MAX_N2+1];
    Square  *p;
    int     i, j, k;

    seen[0]  = 0;   // not used
    index[0] = 0;   // not used

    for ( i = 1 ;  i <= G->N2 ;  ++i)
    {
        for ( k = 1 ;  k <= G->N2 ;  ++k )
            seen[k] = index[k] = 0;

        for ( j = 1 ;  j <= G->N2 ;  ++j)
        {
            p = &G->board[j][i] ;
            if ( p->frozen )
                continue;
            for ( k = 1 ;  k <= G->N2 ;  ++k )
            {
                if ( p->candidate[0] & bit(k) )
                {
                    ++seen[k] ;
                    index[k] = j ;
                }
            }
        }

        for ( k = 1 ;  k <= G->N2 ;  ++k )
        {
            if ( seen[k] == 1 )
            {
                freeze_one ( G, index[k] , i , k ) ;
//printf("square (%d,%d) frozen to piece %d by column optimization\n", 
//                                                        index[k] , i, k);
                ++G->stats.num_frozen_by_column_optimization;
                return true ;
            }
        }

    }   //  end i-loop over columns

    return false ;

}   //  end try_column_optimization


bool try_subsquare_optimization ( Game *G )
/*
    Check each subsquare to see if there is a piece which appears 
    only once as a candidate for the subsquare.  If so, then that 
    piece is frozen to the square for which it is a candidate.

    A true return value means that at least one square was frozen 
    by this function.  A false return value means that this function 
    made no changes to the board.
*/
{
    int     seen[MAX_N2+1];
    int     row_index[MAX_N2+1];
    int     col_index[MAX_N2+1];
    Square  *p;
    int     i, j, k;
    int     ii, jj;

    seen[0]      = 0;   // not used
    row_index[0] = 0;   // not used
    col_index[0] = 0;   // not used

    for ( i = 1 ;  i <= G->N2 ;  i+=G->N )
    {
        for ( j = 1 ;  j <= G->N2 ;  j+=G->N )
        {
            for ( k = 1 ;  k <= G->N2 ;  ++k )
                seen[k] = row_index[k] = col_index[k] = 0 ;

            for ( ii = 0 ;  ii < G->N ;  ++ii )
            {
                for ( jj = 0 ;  jj < G->N ;  ++jj )
                {
                    p = &G->board[i+ii][j+jj] ;
                    if ( p->frozen )
                        continue;
                    for ( k = 1 ;  k <= G->N2 ;  ++k )
                    {
                        if ( p->candidate[0] & bit(k) )
                        {
                            ++seen[k] ;
                            row_index[k] = i+ii ;
                            col_index[k] = j+jj ;
                        }
                    }
                }
            }

            for ( k = 1 ;  k <= G->N2 ;  ++k )
            {
                if ( seen[k] == 1 )
                {
                    freeze_one ( G, row_index[k], col_index[k] , k ) ;
//printf("square (%d,%d) frozen to piece %d by subsquare optimization\n", 
//                                       row_index[k], col_index[k] , k ) ;
                    ++G->stats.num_frozen_by_subsquare_optimization;
                    return true ;
                }
            }

        }   //  end j-loop

    }   //  end i-loop

    return false ;

}   //  end try_subsquare_optimization


void freeze_one ( Game *G, int row, int col, int piece )
/*
    Place piece in square (row, col) and freeze it there.
*/
{
    Square  *p;
    int     i;

    p = &G->board[row][col] ;

    p->cur_value = piece ;
    p->frozen    = true ;

    for ( i = 0 ;  i < G->num_neighbors ;  ++i )
        p->candidate[i] = 0 ;
    p->topmask = -1;
    p->num_candidates = 0;

}   //  end freeze_one


int get_first_candidate ( Game *G, Bitmask b )
/*
    Return the position of the first 1-bit in b.  If there is no 1-bit in 
    the first N2 bits of b, then return zero.
*/
{
    int i;

    for ( i = 1 ;  i <= G->N2 ;  ++i )
    {
        if ( b & bit(i) )
            return i ;
    }

    return 0 ;

}   //  end get_first_candidate


void find_solution ( Game *G )
/*
    Find a solution to the game by scanning the squares, and for each unfrozen
    square s, pick the next available candidate c and place it on s.  Update 
    the candidate lists of the neighbors of s to record the fact that c is no 
    longer available.  Stack the coordinates of square s so that the placement 
    of c on square s can be undone.

    If there are no more candidates c to place on square s, then a deadend has 
    been reached so we must back up:  Pop the stack, changing the square s 
    that we are operating on.  Remove the piece c on the new square.  Update 
    the candidate lists of the neighbors of s so they know c is now available
    again.  Continue scanning the candidates of square s, looking for a 
    different candidate to try to place on s.

    After the last unfrozen square has been processed, the board contains a 
    solution to the original problem.
*/
{
    int     row_stack[MAX_NUM_SQUARES];
    int     col_stack[MAX_NUM_SQUARES];
    int     top;

    Square  *p;
    bool    placed_one ;
    int     i, j, k;
    int     piece_to_place = 0;
    int     rows_processed = 0 ;

    top = -1;

    for ( i = 1 ;  i <= G->N2 ;  ++i)       // Loop over rows
    {
//printf ("\ni = %d\n", i);
        for ( j = 1 ;  j <= G->N2 ;  ++j)   // Loop over columns
        {
//printf ("j = %d\n", j);
            p = &G->board[i][j] ;

            if ( p->frozen )
                continue ;
//printf (" find a candidate > %c\n", map_int_to_alphabet(G, piece_to_place));

            placed_one = false;

            // Find a piece to place on square (i, j) by searching the 
            // candidate list for the square

            for ( k = piece_to_place+1 ;  k <= G->N2 ;  ++k )
            {
                if ( ! (p->candidate[p->topmask] & bit(k)) )
                    continue;

                // Candidate k can be placed on square (i, j), so place it
                // there, update the neighbors of the square that k is no 
                // longer available, and stack (i, j) so we can undo this 
                // placement later, if needed 

                p->cur_value = k ;

                update_neighbors ( G, i, j, k, false ) ;
//printf (" pick %c as candidate\n", map_int_to_alphabet(G, k));

                ++top;
                assert ( top < G->num_squares ) ;
                row_stack[top] = i ;    // Push to stack
                col_stack[top] = j ; 
                placed_one = true;
//printf (" stack (%d, %d)\n", i, j);
                break;

            }   //  end k-loop over candidates for square (i, j)

            if ( ! placed_one )
            {
                assert ( top >= 0 ) ;

                // No candidate was found to place on square (i, j), so 
                // pop the stack to return to an earlier position; and update 
                // neighbors that a piece is now available

                ++G->stats.num_unstackings;
                i = row_stack[top];     // Pop from stack
                j = col_stack[top]; 
                --top;

                p = &G->board[i][j] ;

                piece_to_place = p->cur_value ;
                update_neighbors ( G, i, j, piece_to_place, true ) ;

//printf (" unstack (%d, %d) with cur_value %c\n", i, j, 
//                                map_int_to_alphabet(G, piece_to_place));

                p->cur_value = 0 ;
                --j;
            }
            else
            {
                piece_to_place = 0 ;
            }

        }   //  end j-loop over columns

        ++rows_processed ;

#if 0   // Set to 1 for debugging
if ( rows_processed % 5 == 0 )
  printf ("%d rows processed, %d currently completed\n", 
                                                    rows_processed, i ) ;
printf ("Row i = %d finished:", i);
int m;
for ( m = 1 ;  m <= G->N2 ;  ++m)
  printf (" %c", map_int_to_alphabet(G, G->board[i][m].cur_value));
printf ("\n");
#endif

    }   //  end i-loop over rows

}   //  end find_solution


bool find_candidates ( Game *G )
/*
    For each square, all possible candidates for placing on that square are
    determined.  See find_candidate below for the details of what is changed 
    for each square.

    A true return value means that every non-frozen square has 
        candidates.  

    A false return value means that at least one non-frozen square 
        has no candidates.  As soon as this condition is detected,
        all further processing ceases.
*/
{
    int i, j;
    int retval = true ;

    for ( i = 1 ;  i <= G->N2 ;  ++i)
    {
        for ( j = 1 ;  j <= G->N2 ;  ++j)
        {
            if ( ! find_candidate ( G, i, j ) )
            {
                printf ("ERROR:  Can't find candidates "
                    "for square (%d,%d)\n", i, j);
                retval = false ;
            }
        }
    }
//print_candidates () ;

    return retval ;

}   //  end find_candidates


bool find_candidate ( Game *G, int row, int col )
/*
    All candidates that could be placed on square (row, col) are determined 
    and recorded in a bitmask associated with square (row, col). 

    A true return value means that square (row, col) has candidates
        (or else the square is frozen).

    A false return value means that the square is not frozen and has 
        no candidates.

    Details:  These fields are setup for square (row, col):

        candidate[0] is set to the bitmask of candidates for the square
        num_candidates is set to the number of candidates (= the number
            of 1-bits in candidate[0])
        topmask is set to zero to index candidate[0]
*/
{
    Square  *p;
    int     seen[MAX_N2+1];
    int     neighbor ;
    Bitmask b;
    int     k;

    p = &G->board[row][col] ;

    if ( p->frozen )
        return true;

    for ( k = 0 ;  k <= G->N2 ;  ++k )
        seen[k] = 0;

    for ( k = 0 ;  k < G->num_neighbors ;  ++k )
    {
        neighbor = G->board[p->neighbor_i[k]][p->neighbor_j[k]].cur_value;
        ++seen[neighbor];
    }

    b = 0 ;
    p->num_candidates = 0;

    for ( k = 1 ;  k <= G->N2 ;  ++k )
    {
        if ( seen[k] == 0 ) 
        {
            b = b | bit(k) ;
            ++p->num_candidates;
        }
    }

    p->topmask = 0;
    p->candidate[0] = b;

    return (b != 0);

}   //  end find_candidate


void update_neighbors (
    Game *G,
    int row,
    int col,
    int piece_number,
    bool make_avail )
/*
    The candidate lists of all unfrozen neighbors of square (row, col) 
    are updated by either making piece_number available or not available
    as a candidate.  The square (row, col) must not be frozen.
*/
{
    Square  *p, *q;
    Bitmask b ;
    int     k ;

    p = &G->board[row][col];

    assert ( ! p->frozen ) ;

    // Loop over all neighbors of square (row, col)

    for ( k = 0 ;  k < G->num_neighbors ;  ++k )
    {
        q = &G->board[p->neighbor_i[k]][p->neighbor_j[k]];
        if ( q->frozen )
            continue;
        if ( make_avail )
        {
            // To make piece_number available, simply unstack the 
            //   stack of candidate bitmasks, returning to a new 
            //   top of the stack in which the bit corresponding to
            //   piece_number is set
            --(q->topmask);
            assert ( q->topmask >= 0 ) ;
        }
        else
        {
            // To make piece_number unavailable, create a new bitmask 
            //   from the top of the stack by turning off the bit for 
            //   piece_number, and stack this new bitmask so it becomes 
            //   the top of the stack of candidate bitmasks
            assert ( q->topmask >= 0 ) ;
            b = q->candidate[q->topmask];
            ++(q->topmask);
            q->candidate[q->topmask] = b & (~bit(piece_number));
        }
    }

}   //  end update_neighbors


void find_sub_square ( Game *G, int row, int col, int *srow, int *scol ) 
/*
    Inputs row and col specify an element of the board.
    Outputs *srow and *scol specify the coordinates of the 
    upperleft element of the subsquare that element (row, col)
    belongs to.
*/
{
    *srow = G->N * ( ( row - 1 ) / G->N ) + 1 ;
    *scol = G->N * ( ( col - 1 ) / G->N ) + 1 ;

}   // end find_sub_square


void test_sub_square ( Game *G )    // ***** For debug and test *****
{
    int srow, scol;
    int i, j, k;

    printf ( "Test find_sub_square\n\n" ) ;

    for ( i = 1 ;  i <= G->N2 ;  ++i )
    {
        for ( j = 1 ;  j <= G->N2 ;  ++j )
        {
            find_sub_square ( G, i, j, &srow, &scol ) ;
            printf ( "(%2d,%2d) ", srow, scol ) ;
            if ( j < G->N2 && j % G->N == 0 )
                printf ( "| " ) ;
        }

        printf ( "\n" ) ;

        if ( i < G->N2 && i % G->N == 0 )
        {
            for ( j = 1 ;  j <= G->N ;  ++j )
            {
                for ( k = 1 ;  k <= 8*G->N ;  ++k )
                    printf ("-");
                if ( j > 1 )
                    printf ("-");
                printf ( "%s", (j < G->N ? "+" : "\n") ) ;
            }
        }
    }

}   //  end test_sub_square


void compute_candidate_stats ( Game *G, bool before )
/*
    Count the total number of candidates for all unfrozen squares on the
    board and store as either the "before" or "after" preprocessing count,
    depending on the input bool.
*/
{
    Square  *p;
    int     i, j;

    uint64_t sum_cand = 0;

    for ( i = 1 ;  i <= G->N2 ;  ++i)
    {
        for ( j = 1 ;  j <= G->N2 ;  ++j)
        {
            p = &G->board[i][j];
            if ( p->frozen )
                continue;
            sum_cand += p->num_candidates;
        }
    }

    if (before)
        G->stats.sum_num_candidates_originally = sum_cand;
    else
        G->stats.sum_num_candidates_after_optimization = sum_cand;

}   //  end compute_candidate_stats


void print_board ( Game *G, bool everything )
/*
    Print the board of game G to stdout.  If the 'everything' bool is true, 
    print all the internal details about each square, e.g., its neighbors,
    candidates, etc.
*/
{
    Square  *p;
    char    buf[65];
    int     i, j, k;

    printf ( "\n" ) ;

    if ( G->N != 3 )
        printf ( "//N=%d\n\n", G->N ) ;

    for ( i = 1 ;  i <= G->N2 ;  ++i)
    {
        printf ( "  " ) ;

        for ( j = 1 ;  j <= G->N2 ;  ++j)
        {
            p = &G->board[i][j] ;

            printf ( "%c", map_int_to_alphabet(G, p->cur_value) ) ;

            if ( j < G->N2 && j % G->N == 0 )
                printf ( "   " ) ;
            else
                printf ( " " ) ;
        }

        printf ( "\n" ) ;

        if ( i < G->N2 && i % G->N == 0 )
            printf ( "\n" ) ;
    }

    if ( ! everything )
        return ;        

    printf ( "\n" ) ;

    for ( i = 1 ;  i <= G->N2 ;  ++i)
    {
        for ( j = 1 ;  j <= G->N2 ;  ++j)
        {
            p = &G->board[i][j] ;

            printf ( "(%2d,%2d):  cur_value=%2d  frozen=%d  "
                "topmask=%d  num_candidates=%d\n", 
                i, j, p->cur_value, p->frozen, p->topmask,
                p->num_candidates ) ;

            // print neighbor squares
            printf ( "           " ) ;
            for ( k = 0 ;  k < G->num_neighbors ;  ++k )
                printf ( " (%2d,%2d)", p->neighbor_i[k],
                    p->neighbor_j[k] ) ;
            printf ( "\n" ) ;

            // print candidates
            for ( k = 0 ;  k <= p->topmask ;  ++k )
            {
                convert_bitmask ( p->candidate[k], buf ) ;
                printf ( "            %2d: %s\n" , k, buf ) ;
            }

        }
    }

}   //  end print_board


void print_statistics ( Game *G )
/*
    Print statistics about the game to stdout:  the initial setup, the results 
    of preprocessing, and the solution.
*/
{
    int     num_empty_squares_originally;
    int     num_optimizations;
    int     num_occupied_squares_after_optimization;
    int     num_empty_squares_after_optimization;
    double  cand_per_empty_before;
    double  cand_per_empty_after;

    num_empty_squares_originally = 
        G->num_squares - G->stats.num_occupied_squares_originally;

    num_optimizations = 
        G->stats.num_frozen_as_only_candidate +
        G->stats.num_frozen_by_row_optimization +
        G->stats.num_frozen_by_column_optimization +
        G->stats.num_frozen_by_subsquare_optimization;

    num_occupied_squares_after_optimization =
        G->stats.num_occupied_squares_originally + num_optimizations;

    num_empty_squares_after_optimization =
        G->num_squares - num_occupied_squares_after_optimization;

    if (num_empty_squares_originally == 0)
        cand_per_empty_before = 0;
    else
        cand_per_empty_before = G->stats.sum_num_candidates_originally 
                                    / (double)num_empty_squares_originally;

    if (num_empty_squares_after_optimization == 0)
        cand_per_empty_after = 0;
    else
        cand_per_empty_after = G->stats.sum_num_candidates_after_optimization 
                            / (double)num_empty_squares_after_optimization;

    printf("\n");
    printf("statistics\n");
    printf("  original board\n");
    printf("    number of occupied squares:       %4d\n", G->stats.num_occupied_squares_originally);
    printf("    number of empty squares:          %4d\n", num_empty_squares_originally);
    printf("    total number of squares:          %4d\n", G->num_squares);
    printf("    sum of no. candidates       %10"PRIu64"\n", G->stats.sum_num_candidates_originally);
    printf("    candidates/empty square         %6.1f\n", cand_per_empty_before);
    printf("  preprocessing\n");
    printf("    number of only-one candidates:    %4d\n", G->stats.num_frozen_as_only_candidate);
    printf("    number of row optimizations:      %4d\n", G->stats.num_frozen_by_row_optimization);
    printf("    number of column optimizations:   %4d\n", G->stats.num_frozen_by_column_optimization);
    printf("    number of subsquare optimizations:%4d\n", G->stats.num_frozen_by_subsquare_optimization);
    printf("    total number of optimizations:    %4d\n", num_optimizations);
    printf("  after optimization\n");
    printf("    number of occupied squares:       %4d\n", num_occupied_squares_after_optimization);
    printf("    number of empty squares:          %4d\n", num_empty_squares_after_optimization);
    printf("    total number of squares:          %4d\n", G->num_squares);
    printf("    sum of no. candidates       %10"PRIu64"\n", G->stats.sum_num_candidates_after_optimization);
    printf("    candidates/empty square         %6.1f\n", cand_per_empty_after);
    printf("  backtracking\n");
    printf("    number of unstackings:       %9d\n", G->stats.num_unstackings);

}   //  end print_statistics

/*
statistics
  original board
    number of unoccupied squares:     xxxx
    number of empty squares:          xxxx
    total number of squares:          xxxx
    sum of no. candidates        xxxxxxxxx
    candidates/empty square         xxxx.x
  preprocessing
    number of only-one candidates:    xxxx
    number of row optimizations:      xxxx
    number of column optimizations:   xxxx
    number of subsquare optimizations:xxxx
    total number of optimizations:    xxxx
  after optimization
    number of unoccupied squares:     xxxx
    number of empty squares:          xxxx
    total number of squares:          xxxx
    sum of no. candidates        xxxxxxxxx
    candidates/empty square         xxxx.x
  backtracking
    number of unstackings:       xxxxxxxxx
*/


void print_candidates ( Game *G )   // ***** For debug and test *****
/*
    Print to stdout all candidates for each square.  This function is only 
    used for debugging and testing.
*/
{
    Square  *p;
    int     i, j, k;

    printf ( "\n" ) ;

    for ( i = 1 ;  i <= G->N2 ;  ++i)
    {
        for ( j = 1 ;  j <= G->N2 ;  ++j)
        {
            p = &G->board[i][j] ;

            printf ( "Square (%d, %d) ", i, j );

            if ( p->frozen )
            {
                printf ( "current value:  %c\n", 
                                    map_int_to_alphabet(G, p->cur_value) ) ;
                continue ;
            }

            printf ( "candidates: " ) ;

            for ( k = 1 ;  k <= G->N2 ;  ++k )
            {
                if ( p->candidate[p->topmask] & bit(k) )
                    printf ( " %c", map_int_to_alphabet(G, k) ) ;
            }
            printf ( "\n" ) ;
        }
    }

}   //  end print_candidates
/*
(%2d,%2d):  cur_value=%2d  frozen=%d  topmask=%d  num_candidates=%d
            (%2d,%2d) (%2d,%2d) (%2d,%2d) ...
            0: 8000000000000000
            1: 6400000000000000
*/


void print_bit_mask_array ( Bitmask b[]  )  // ***** For debug and test *****
/*
    Print to stdout the 64 bitmasks in input array b.  This function is only 
    used for debugging and testing.
*/
{
    int     i ; 
    char    buf[65];

    printf ( "bitmask array:\n" ) ;

    for ( i = 0 ;  i < 64 ;  ++i )
    {
        convert_bitmask ( b[i], buf ) ;
        printf ( "  %s\n" , buf ) ;
    }   

}   //  end print_bit_mask_array 


void convert_bitmask ( Bitmask b, char *s )
/*
    Convert b to a string of 64 '0' and '1' characters stored in s,
    null-terminated.  It is the caller's responsibility to make sure 
    s points at an area at least 65 bytes long.
*/
{
    int i ; 

    for ( i = 0 ;  i < 64 ;  ++i )
    {
        *s++ = ( b & 0x8000000000000000UL ? '1' : '0' ) ; 
        b <<= 1 ;
    }

    *s = '\0' ;

}   //  end convert_bitmask


Bitmask bit(int i)
/*
    Input i is a bit number in [1, 64].

    The return value is a 64-bit bitmask with a single 1-bit set in position 
    i, where position 1 is the leftmost bit and position 64 is the rightmost
    bit.
 
        position   bitmask
        -------------------------------
           1       0x8000000000000000UL
           2       0x4000000000000000UL
           3       0x2000000000000000UL
         ...       ...
          63       0x0000000000000002UL
          64       0x0000000000000001UL

*/
{
    static Bitmask bit1[64] = { 0 };

    if (bit1[0] == 0)
    {
        init_bit_mask_array  ( bit1 );
//      print_bit_mask_array ( bit1 );
    }

    assert ( 1 <= i && i <= 64 ) ;

    return bit1[i-1] ;

}   // end bit


void init_bit_mask_array ( Bitmask b[]  )
/*
    The input is a bitmask array of at least 64 elements.  The first 64 
    elements are set to bitmasks with a single 1-bit per mask, as shown 
    in the comment to the bit function above.
*/
{
    uint64_t one_bit ;
    int      i ; 

    one_bit = 0x8000000000000000UL;

    for ( i = 0 ;  i < 64 ;  ++i )
    {
        b[i] = one_bit;
        one_bit >>= 1;
    }

    assert ( one_bit == 0 );

}   //  end init_bit_mask_array

// end suduko.c
