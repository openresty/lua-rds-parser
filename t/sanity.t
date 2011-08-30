# vi:ft=

use strict;
use warnings;

use t::RdsParser;
plan tests => 1 * blocks();

run_tests();

__DATA__

=== TEST 1: basic
--- rds eval
"\x{00}". # endian
"\x{03}\x{00}\x{00}\x{00}". # format version 0.0.3
"\x{00}". # result type
"\x{00}\x{00}".  # std errcode
"\x{00}\x{00}" . # driver errcode
"\x{00}\x{00}".  # driver errstr len
"".  # driver errstr data
"\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}".  # rows affected
"\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}\x{00}".  # insert id
"\x{02}\x{00}".  # col count
"\x{09}\x{00}".  # std col type (integer)
"\x{03}\x{00}".  # drizzle col type
"\x{02}\x{00}".     # col name len
"id".   # col name data
"\x{13}\x{80}".  # std col type (blob/str)
"\x{fc}\x{00}".  # drizzle col type
"\x{04}\x{00}".  # col name len
"name".  # col name data
"\x{01}".  # valid row flag
"\x{01}\x{00}\x{00}\x{00}".  # field len
"2".  # field data
"\x{ff}\x{ff}\x{ff}\x{ff}".  # field len
"".  # field data
"\x{01}".  # valid row flag
"\x{01}\x{00}\x{00}\x{00}".  # field len
"3".  # field data
"\x{03}\x{00}\x{00}\x{00}".  # field len
"bob".  # field data
"\x{00}"  # row list terminator
--- out

