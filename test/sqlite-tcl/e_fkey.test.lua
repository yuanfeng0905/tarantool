#!./tcltestrunner.lua

# 2009 October 7
#
# The author disclaims copyright to this source code.  In place of
# a legal notice, here is a blessing:
#
#    May you do good and not evil.
#    May you find forgiveness for yourself and forgive others.
#    May you share freely, never taking more than you give.
#
#***********************************************************************
#
# This file implements tests to verify the "testable statements" in the
# foreignkeys.in document.
#
# The tests in this file are arranged to mirror the structure of 
# foreignkey.in, with one exception: The statements in section 2, which 
# deals with enabling/disabling foreign key support, is tested first,
# before section 1. This is because some statements in section 2 deal
# with builds that do not include complete foreign key support (because
# either SQLITE_OMIT_TRIGGER or SQLITE_OMIT_FOREIGN_KEY was defined
# at build time).
#

set testdir [file dirname $argv0]
source $testdir/tester.tcl

proc eqp {sql {db db}} { uplevel execsql [list "EXPLAIN QUERY PLAN $sql"] $db }

# ###########################################################################
# ### SECTION 2: Enabling Foreign Key Support
# ###########################################################################

# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-33710-56344 In order to use foreign key constraints in
# # SQLite, the library must be compiled with neither
# # SQLITE_OMIT_FOREIGN_KEY or SQLITE_OMIT_TRIGGER defined.
# #
# ifcapable trigger&&foreignkey {
#   do_test e_fkey-1 {
#     execsql {
#       PRAGMA foreign_keys = ON;
#       CREATE TABLE p(i PRIMARY KEY);
#       CREATE TABLE c(j REFERENCES p ON UPDATE CASCADE);
#       INSERT INTO p VALUES('hello');
#       INSERT INTO c VALUES('hello');
#       UPDATE p SET i = 'world';
#       SELECT * FROM c;
#     }
#   } {world}
# }

# #-------------------------------------------------------------------------
# # Test the effects of defining OMIT_TRIGGER but not OMIT_FOREIGN_KEY.
# #
# # EVIDENCE-OF: R-44697-61543 If SQLITE_OMIT_TRIGGER is defined but
# # SQLITE_OMIT_FOREIGN_KEY is not, then SQLite behaves as it did prior to
# # version 3.6.19 - foreign key definitions are parsed and may be queried
# # using PRAGMA foreign_key_list, but foreign key constraints are not
# # enforced.
# #
# # Specifically, test that "PRAGMA foreign_keys" is a no-op in this case.
# # When using the pragma to query the current setting, 0 rows are returned.
# #
# # EVIDENCE-OF: R-22567-44039 The PRAGMA foreign_keys command is a no-op
# # in this configuration.
# #
# # EVIDENCE-OF: R-41784-13339 Tip: If the command "PRAGMA foreign_keys"
# # returns no data instead of a single row containing "0" or "1", then
# # the version of SQLite you are using does not support foreign keys
# # (either because it is older than 3.6.19 or because it was compiled
# # with SQLITE_OMIT_FOREIGN_KEY or SQLITE_OMIT_TRIGGER defined).
# #
# reset_db
# ifcapable !trigger&&foreignkey {
#   do_test e_fkey-2.1 {
#     execsql {
#       PRAGMA foreign_keys = ON;
#       CREATE TABLE p(i PRIMARY KEY);
#       CREATE TABLE c(j REFERENCES p ON UPDATE CASCADE);
#       INSERT INTO p VALUES('hello');
#       INSERT INTO c VALUES('hello');
#       UPDATE p SET i = 'world';
#       SELECT * FROM c;
#     }
#   } {hello}
#   do_test e_fkey-2.2 {
#     execsql { PRAGMA foreign_key_list(c) }
#   } {0 0 p j {} CASCADE {NO ACTION} NONE}
#   do_test e_fkey-2.3 {
#     execsql { PRAGMA foreign_keys }
#   } {}
# }


# #-------------------------------------------------------------------------
# # Test the effects of defining OMIT_FOREIGN_KEY.
# #
# # EVIDENCE-OF: R-58428-36660 If OMIT_FOREIGN_KEY is defined, then
# # foreign key definitions cannot even be parsed (attempting to specify a
# # foreign key definition is a syntax error).
# #
# # Specifically, test that foreign key constraints cannot even be parsed 
# # in such a build.
# #
# reset_db
# ifcapable !foreignkey {
#   do_test e_fkey-3.1 {
#     execsql { CREATE TABLE p(i PRIMARY KEY) }
#     catchsql { CREATE TABLE c(j REFERENCES p ON UPDATE CASCADE) }
#   } {1 {near "ON": syntax error}}
#   do_test e_fkey-3.2 {
#     # This is allowed, as in this build, "REFERENCES" is not a keyword.
#     # The declared datatype of column j is "REFERENCES p".
#     execsql { CREATE TABLE c(j REFERENCES p) }
#   } {}
#   do_test e_fkey-3.3 {
#     execsql { PRAGMA table_info(c) }
#   } {0 j {REFERENCES p} 0 {} 0}
#   do_test e_fkey-3.4 {
#     execsql { PRAGMA foreign_key_list(c) }
#   } {}
#   do_test e_fkey-3.5 {
#     execsql { PRAGMA foreign_keys }
#   } {}
# }

# ifcapable !foreignkey||!trigger { finish_test ; return }
# reset_db


# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-07280-60510 Assuming the library is compiled with
# # foreign key constraints enabled, it must still be enabled by the
# # application at runtime, using the PRAGMA foreign_keys command.
# #
# # This also tests that foreign key constraints are disabled by default.
# #
# # EVIDENCE-OF: R-44261-39702 Foreign key constraints are disabled by
# # default (for backwards compatibility), so must be enabled separately
# # for each database connection.
# #
# drop_all_tables
# do_test e_fkey-4.1 {
#   execsql {
#     CREATE TABLE p(i PRIMARY KEY);
#     CREATE TABLE c(j REFERENCES p ON UPDATE CASCADE);
#     INSERT INTO p VALUES('hello');
#     INSERT INTO c VALUES('hello');
#     UPDATE p SET i = 'world';
#     SELECT * FROM c;
#   } 
# } {hello}
# do_test e_fkey-4.2 {
#   execsql {
#     DELETE FROM c;
#     DELETE FROM p;
#     PRAGMA foreign_keys = ON;
#     INSERT INTO p VALUES('hello');
#     INSERT INTO c VALUES('hello');
#     UPDATE p SET i = 'world';
#     SELECT * FROM c;
#   } 
# } {world}

# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-08013-37737 The application can also use a PRAGMA
# # foreign_keys statement to determine if foreign keys are currently
# # enabled.

# #
# # This also tests the example code in section 2 of foreignkeys.in.
# #
# # EVIDENCE-OF: R-11255-19907
# # 
# reset_db
# do_test e_fkey-5.1 {
#   execsql { PRAGMA foreign_keys }
# } {0}
# do_test e_fkey-5.2 {
#   execsql { 
#     PRAGMA foreign_keys = ON;
#     PRAGMA foreign_keys;
#   }
# } {1}
# do_test e_fkey-5.3 {
#   execsql { 
#     PRAGMA foreign_keys = OFF;
#     PRAGMA foreign_keys;
#   }
# } {0}

# #-------------------------------------------------------------------------
# # Test that it is not possible to enable or disable foreign key support
# # while not in auto-commit mode.
# #
# # EVIDENCE-OF: R-46649-58537 It is not possible to enable or disable
# # foreign key constraints in the middle of a multi-statement transaction
# # (when SQLite is not in autocommit mode). Attempting to do so does not
# # return an error; it simply has no effect.
# #
# reset_db
# do_test e_fkey-6.1 {
#   execsql {
#     PRAGMA foreign_keys = ON;
#     CREATE TABLE t1(a UNIQUE, b);
#     CREATE TABLE t2(c, d REFERENCES t1(a));
#     INSERT INTO t1 VALUES(1, 2);
#     INSERT INTO t2 VALUES(2, 1);
#     BEGIN;
#       PRAGMA foreign_keys = OFF;
#   }
#   catchsql {
#       DELETE FROM t1
#   }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-6.2 {
#   execsql { PRAGMA foreign_keys }
# } {1}
# do_test e_fkey-6.3 {
#   execsql {
#     COMMIT;
#     PRAGMA foreign_keys = OFF;
#     BEGIN;
#       PRAGMA foreign_keys = ON;
#       DELETE FROM t1;
#       PRAGMA foreign_keys;
#   }
# } {0}
# do_test e_fkey-6.4 {
#   execsql COMMIT
# } {}

# ###########################################################################
# ### SECTION 1: Introduction to Foreign Key Constraints
# ###########################################################################
# execsql "PRAGMA foreign_keys = ON"

# #-------------------------------------------------------------------------
# # Verify that the syntax in the first example in section 1 is valid.
# #
# # EVIDENCE-OF: R-04042-24825 To do so, a foreign key definition may be
# # added by modifying the declaration of the track table to the
# # following: CREATE TABLE track( trackid INTEGER, trackname TEXT,
# # trackartist INTEGER, FOREIGN KEY(trackartist) REFERENCES
# # artist(artistid) );
# #
# do_test e_fkey-7.1 {
#   execsql {
#     CREATE TABLE artist(
#       artistid    INTEGER PRIMARY KEY, 
#       artistname  TEXT
#     );
#     CREATE TABLE track(
#       trackid     INTEGER, 
#       trackname   TEXT, 
#       trackartist INTEGER,
#       FOREIGN KEY(trackartist) REFERENCES artist(artistid)
#     );
#   }
# } {}

# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-61362-32087 Attempting to insert a row into the track
# # table that does not correspond to any row in the artist table will
# # fail,
# #
# do_test e_fkey-8.1 {
#   catchsql { INSERT INTO track VALUES(1, 'track 1', 1) }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-8.2 {
#   execsql { INSERT INTO artist VALUES(2, 'artist 1') }
#   catchsql { INSERT INTO track VALUES(1, 'track 1', 1) }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-8.2 {
#   execsql { INSERT INTO track VALUES(1, 'track 1', 2) }
# } {}

# #-------------------------------------------------------------------------
# # Attempting to delete a row from the 'artist' table while there are 
# # dependent rows in the track table also fails.
# #
# # EVIDENCE-OF: R-24401-52400 as will attempting to delete a row from the
# # artist table when there exist dependent rows in the track table
# #
# do_test e_fkey-9.1 {
#   catchsql { DELETE FROM artist WHERE artistid = 2 }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-9.2 {
#   execsql { 
#     DELETE FROM track WHERE trackartist = 2;
#     DELETE FROM artist WHERE artistid = 2;
#   }
# } {}

# #-------------------------------------------------------------------------
# # If the foreign key column (trackartist) in table 'track' is set to NULL,
# # there is no requirement for a matching row in the 'artist' table.
# #
# # EVIDENCE-OF: R-23980-48859 There is one exception: if the foreign key
# # column in the track table is NULL, then no corresponding entry in the
# # artist table is required.
# #
# do_test e_fkey-10.1 {
#   execsql {
#     INSERT INTO track VALUES(1, 'track 1', NULL);
#     INSERT INTO track VALUES(2, 'track 2', NULL);
#   }
# } {}
# do_test e_fkey-10.2 {
#   execsql { SELECT * FROM artist }
# } {}
# do_test e_fkey-10.3 {
#   # Setting the trackid to a non-NULL value fails, of course.
#   catchsql { UPDATE track SET trackartist = 5 WHERE trackid = 1 }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-10.4 {
#   execsql {
#     INSERT INTO artist VALUES(5, 'artist 5');
#     UPDATE track SET trackartist = 5 WHERE trackid = 1;
#   }
#   catchsql { DELETE FROM artist WHERE artistid = 5}
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-10.5 {
#   execsql { 
#     UPDATE track SET trackartist = NULL WHERE trackid = 1;
#     DELETE FROM artist WHERE artistid = 5;
#   }
# } {}

# #-------------------------------------------------------------------------
# # Test that the following is true fo all rows in the track table:
# #
# #   trackartist IS NULL OR 
# #   EXISTS(SELECT 1 FROM artist WHERE artistid=trackartist)
# #
# # EVIDENCE-OF: R-52486-21352 Expressed in SQL, this means that for every
# # row in the track table, the following expression evaluates to true:
# # trackartist IS NULL OR EXISTS(SELECT 1 FROM artist WHERE
# # artistid=trackartist)

# # This procedure executes a test case to check that statement 
# # R-52486-21352 is true after executing the SQL statement passed.
# # as the second argument.
# proc test_r52486_21352 {tn sql} {
#   set res [catchsql $sql]
#   set results {
#     {0 {}} 
#     {1 {UNIQUE constraint failed: artist.artistid}} 
#     {1 {FOREIGN KEY constraint failed}}
#   }
#   if {[lsearch $results $res]<0} {
#     error $res
#   }

#   do_test e_fkey-11.$tn {
#     execsql {
#       SELECT count(*) FROM track WHERE NOT (
#         trackartist IS NULL OR 
#         EXISTS(SELECT 1 FROM artist WHERE artistid=trackartist)
#       )
#     }
#   } {0}
# }

# # Execute a series of random INSERT, UPDATE and DELETE operations
# # (some of which may fail due to FK or PK constraint violations) on 
# # the two tables in the example schema. Test that R-52486-21352
# # is true after executing each operation.
# #
# set Template {
#   {INSERT INTO track VALUES($t, 'track $t', $a)}
#   {DELETE FROM track WHERE trackid = $t}
#   {UPDATE track SET trackartist = $a WHERE trackid = $t}
#   {INSERT INTO artist VALUES($a, 'artist $a')}
#   {DELETE FROM artist WHERE artistid = $a}
#   {UPDATE artist SET artistid = $a2 WHERE artistid = $a}
# }
# for {set i 0} {$i < 500} {incr i} {
#   set a   [expr int(rand()*10)]
#   set a2  [expr int(rand()*10)]
#   set t   [expr int(rand()*50)]
#   set sql [subst [lindex $Template [expr int(rand()*6)]]]

#   test_r52486_21352 $i $sql
# }

# #-------------------------------------------------------------------------
# # Check that a NOT NULL constraint can be added to the example schema
# # to prohibit NULL child keys from being inserted.
# #
# # EVIDENCE-OF: R-42412-59321 Tip: If the application requires a stricter
# # relationship between artist and track, where NULL values are not
# # permitted in the trackartist column, simply add the appropriate "NOT
# # NULL" constraint to the schema.
# #
# drop_all_tables
# do_test e_fkey-12.1 {
#   execsql {
#     CREATE TABLE artist(
#       artistid    INTEGER PRIMARY KEY, 
#       artistname  TEXT
#     );
#     CREATE TABLE track(
#       trackid     INTEGER, 
#       trackname   TEXT, 
#       trackartist INTEGER NOT NULL,
#       FOREIGN KEY(trackartist) REFERENCES artist(artistid)
#     );
#   }
# } {}
# do_test e_fkey-12.2 {
#   catchsql { INSERT INTO track VALUES(14, 'Mr. Bojangles', NULL) }
# } {1 {NOT NULL constraint failed: track.trackartist}}

# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-16127-35442
# #
# # Test an example from foreignkeys.html.
# #
# drop_all_tables
# do_test e_fkey-13.1 {
#   execsql {
#     CREATE TABLE artist(
#       artistid    INTEGER PRIMARY KEY, 
#       artistname  TEXT
#     );
#     CREATE TABLE track(
#       trackid     INTEGER, 
#       trackname   TEXT, 
#       trackartist INTEGER,
#       FOREIGN KEY(trackartist) REFERENCES artist(artistid)
#     );
#     INSERT INTO artist VALUES(1, 'Dean Martin');
#     INSERT INTO artist VALUES(2, 'Frank Sinatra');
#     INSERT INTO track VALUES(11, 'That''s Amore', 1);
#     INSERT INTO track VALUES(12, 'Christmas Blues', 1);
#     INSERT INTO track VALUES(13, 'My Way', 2);
#   }
# } {}
# do_test e_fkey-13.2 {
#   catchsql { INSERT INTO track VALUES(14, 'Mr. Bojangles', 3) }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-13.3 {
#   execsql { INSERT INTO track VALUES(14, 'Mr. Bojangles', NULL) }
# } {}
# do_test e_fkey-13.4 {
#   catchsql { 
#     UPDATE track SET trackartist = 3 WHERE trackname = 'Mr. Bojangles';
#   }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-13.5 {
#   execsql {
#     INSERT INTO artist VALUES(3, 'Sammy Davis Jr.');
#     UPDATE track SET trackartist = 3 WHERE trackname = 'Mr. Bojangles';
#     INSERT INTO track VALUES(15, 'Boogie Woogie', 3);
#   }
# } {}

# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-15958-50233
# #
# # Test the second example from the first section of foreignkeys.html.
# #
# do_test e_fkey-14.1 {
#   catchsql {
#     DELETE FROM artist WHERE artistname = 'Frank Sinatra';
#   }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-14.2 {
#   execsql {
#     DELETE FROM track WHERE trackname = 'My Way';
#     DELETE FROM artist WHERE artistname = 'Frank Sinatra';
#   }
# } {}
# do_test e_fkey-14.3 {
#   catchsql {
#     UPDATE artist SET artistid=4 WHERE artistname = 'Dean Martin';
#   }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-14.4 {
#   execsql {
#     DELETE FROM track WHERE trackname IN('That''s Amore', 'Christmas Blues');
#     UPDATE artist SET artistid=4 WHERE artistname = 'Dean Martin';
#   }
# } {}


# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-56032-24923 The foreign key constraint is satisfied if
# # for each row in the child table either one or more of the child key
# # columns are NULL, or there exists a row in the parent table for which
# # each parent key column contains a value equal to the value in its
# # associated child key column.
# #
# # Test also that the usual comparison rules are used when testing if there 
# # is a matching row in the parent table of a foreign key constraint.
# #
# # EVIDENCE-OF: R-57765-12380 In the above paragraph, the term "equal"
# # means equal when values are compared using the rules specified here.
# #
# drop_all_tables
# do_test e_fkey-15.1 {
#   execsql {
#     CREATE TABLE par(p PRIMARY KEY);
#     CREATE TABLE chi(c REFERENCES par);

#     INSERT INTO par VALUES(1);
#     INSERT INTO par VALUES('1');
#     INSERT INTO par VALUES(X'31');
#     SELECT typeof(p) FROM par;
#   }
# } {integer text blob}

# proc test_efkey_45 {tn isError sql} {
#   do_test e_fkey-15.$tn.1 "
#     catchsql {$sql}
#   " [lindex {{0 {}} {1 {FOREIGN KEY constraint failed}}} $isError]

#   do_test e_fkey-15.$tn.2 {
#     execsql {
#       SELECT * FROM chi WHERE c IS NOT NULL AND c NOT IN (SELECT p FROM par)
#     }
#   } {}
# }

# test_efkey_45 1 0 "INSERT INTO chi VALUES(1)"
# test_efkey_45 2 1 "INSERT INTO chi VALUES('1.0')"
# test_efkey_45 3 0 "INSERT INTO chi VALUES('1')"
# test_efkey_45 4 1 "DELETE FROM par WHERE p = '1'"
# test_efkey_45 5 0 "DELETE FROM chi WHERE c = '1'"
# test_efkey_45 6 0 "DELETE FROM par WHERE p = '1'"
# test_efkey_45 7 1 "INSERT INTO chi VALUES('1')"
# test_efkey_45 8 0 "INSERT INTO chi VALUES(X'31')"
# test_efkey_45 9 1 "INSERT INTO chi VALUES(X'32')"

# #-------------------------------------------------------------------------
# # Specifically, test that when comparing child and parent key values the
# # default collation sequence of the parent key column is used.
# #
# # EVIDENCE-OF: R-15796-47513 When comparing text values, the collating
# # sequence associated with the parent key column is always used.
# #
# drop_all_tables
# do_test e_fkey-16.1 {
#   execsql {
#     CREATE TABLE t1(a COLLATE nocase PRIMARY KEY);
#     CREATE TABLE t2(b REFERENCES t1);
#   }
# } {}
# do_test e_fkey-16.2 {
#   execsql {
#     INSERT INTO t1 VALUES('oNe');
#     INSERT INTO t2 VALUES('one');
#     INSERT INTO t2 VALUES('ONE');
#     UPDATE t2 SET b = 'OnE';
#     UPDATE t1 SET a = 'ONE';
#   }
# } {}
# do_test e_fkey-16.3 {
#   catchsql { UPDATE t2 SET b = 'two' WHERE rowid = 1 }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-16.4 {
#   catchsql { DELETE FROM t1 WHERE rowid = 1 }
# } {1 {FOREIGN KEY constraint failed}}

# #-------------------------------------------------------------------------
# # Specifically, test that when comparing child and parent key values the
# # affinity of the parent key column is applied to the child key value
# # before the comparison takes place.
# #
# # EVIDENCE-OF: R-04240-13860 When comparing values, if the parent key
# # column has an affinity, then that affinity is applied to the child key
# # value before the comparison is performed.
# #
# drop_all_tables
# do_test e_fkey-17.1 {
#   execsql {
#     CREATE TABLE t1(a NUMERIC PRIMARY KEY);
#     CREATE TABLE t2(b TEXT REFERENCES t1);
#   }
# } {}
# do_test e_fkey-17.2 {
#   execsql {
#     INSERT INTO t1 VALUES(1);
#     INSERT INTO t1 VALUES(2);
#     INSERT INTO t1 VALUES('three');
#     INSERT INTO t2 VALUES('2.0');
#     SELECT b, typeof(b) FROM t2;
#   }
# } {2.0 text}
# do_test e_fkey-17.3 {
#   execsql { SELECT typeof(a) FROM t1 }
# } {integer integer text}
# do_test e_fkey-17.4 {
#   catchsql { DELETE FROM t1 WHERE rowid = 2 }
# } {1 {FOREIGN KEY constraint failed}}

# ###########################################################################
# ### SECTION 3: Required and Suggested Database Indexes
# ###########################################################################

# #-------------------------------------------------------------------------
# # A parent key must be either a PRIMARY KEY, subject to a UNIQUE 
# # constraint, or have a UNIQUE index created on it.
# #
# # EVIDENCE-OF: R-13435-26311 Usually, the parent key of a foreign key
# # constraint is the primary key of the parent table. If they are not the
# # primary key, then the parent key columns must be collectively subject
# # to a UNIQUE constraint or have a UNIQUE index.
# # 
# # Also test that if a parent key is not subject to a PRIMARY KEY or UNIQUE
# # constraint, but does have a UNIQUE index created on it, then the UNIQUE index
# # must use the default collation sequences associated with the parent key
# # columns.
# #
# # EVIDENCE-OF: R-00376-39212 If the parent key columns have a UNIQUE
# # index, then that index must use the collation sequences that are
# # specified in the CREATE TABLE statement for the parent table.
# #
# drop_all_tables
# do_test e_fkey-18.1 {
#   execsql {
#     CREATE TABLE t2(a REFERENCES t1(x));
#   }
# } {}
# proc test_efkey_57 {tn isError sql} {
#   catchsql { DROP TABLE t1 }
#   execsql $sql
#   do_test e_fkey-18.$tn {
#     catchsql { INSERT INTO t2 VALUES(NULL) }
#   } [lindex {{0 {}} {/1 {foreign key mismatch - ".*" referencing ".*"}/}} \
#      $isError]
# }
# test_efkey_57 2 0 { CREATE TABLE t1(x PRIMARY KEY) }
# test_efkey_57 3 0 { CREATE TABLE t1(x UNIQUE) }
# test_efkey_57 4 0 { CREATE TABLE t1(x); CREATE UNIQUE INDEX t1i ON t1(x) }
# test_efkey_57 5 1 { 
#   CREATE TABLE t1(x); 
#   CREATE UNIQUE INDEX t1i ON t1(x COLLATE nocase);
# }
# test_efkey_57 6 1 { CREATE TABLE t1(x) }
# test_efkey_57 7 1 { CREATE TABLE t1(x, y, PRIMARY KEY(x, y)) }
# test_efkey_57 8 1 { CREATE TABLE t1(x, y, UNIQUE(x, y)) }
# test_efkey_57 9 1 { 
#   CREATE TABLE t1(x, y); 
#   CREATE UNIQUE INDEX t1i ON t1(x, y);
# }


# #-------------------------------------------------------------------------
# # This block tests an example in foreignkeys.html. Several testable
# # statements refer to this example, as follows
# #
# # EVIDENCE-OF: R-27484-01467
# #
# # FK Constraints on child1, child2 and child3 are Ok.
# #
# # Problem with FK on child4:
# #
# # EVIDENCE-OF: R-51039-44840 The foreign key declared as part of table
# # child4 is an error because even though the parent key column is
# # indexed, the index is not UNIQUE.
# #
# # Problem with FK on child5:
# #
# # EVIDENCE-OF: R-01060-48788 The foreign key for table child5 is an
# # error because even though the parent key column has a unique index,
# # the index uses a different collating sequence.
# #
# # Problem with FK on child6 and child7:
# #
# # EVIDENCE-OF: R-63088-37469 Tables child6 and child7 are incorrect
# # because while both have UNIQUE indices on their parent keys, the keys
# # are not an exact match to the columns of a single UNIQUE index.
# #
# drop_all_tables
# do_test e_fkey-19.1 {
#   execsql {
#     CREATE TABLE parent(a PRIMARY KEY, b UNIQUE, c, d, e, f);
#     CREATE UNIQUE INDEX i1 ON parent(c, d);
#     CREATE INDEX i2 ON parent(e);
#     CREATE UNIQUE INDEX i3 ON parent(f COLLATE nocase);

#     CREATE TABLE child1(f, g REFERENCES parent(a));                       -- Ok
#     CREATE TABLE child2(h, i REFERENCES parent(b));                       -- Ok
#     CREATE TABLE child3(j, k, FOREIGN KEY(j, k) REFERENCES parent(c, d)); -- Ok
#     CREATE TABLE child4(l, m REFERENCES parent(e));                       -- Err
#     CREATE TABLE child5(n, o REFERENCES parent(f));                       -- Err
#     CREATE TABLE child6(p, q, FOREIGN KEY(p,q) REFERENCES parent(b, c));  -- Err
#     CREATE TABLE child7(r REFERENCES parent(c));                          -- Err
#   }
# } {}
# do_test e_fkey-19.2 {
#   execsql {
#     INSERT INTO parent VALUES(1, 2, 3, 4, 5, 6);
#     INSERT INTO child1 VALUES('xxx', 1);
#     INSERT INTO child2 VALUES('xxx', 2);
#     INSERT INTO child3 VALUES(3, 4);
#   }
# } {}
# do_test e_fkey-19.2 {
#   catchsql { INSERT INTO child4 VALUES('xxx', 5) }
# } {1 {foreign key mismatch - "child4" referencing "parent"}}
# do_test e_fkey-19.3 {
#   catchsql { INSERT INTO child5 VALUES('xxx', 6) }
# } {1 {foreign key mismatch - "child5" referencing "parent"}}
# do_test e_fkey-19.4 {
#   catchsql { INSERT INTO child6 VALUES(2, 3) }
# } {1 {foreign key mismatch - "child6" referencing "parent"}}
# do_test e_fkey-19.5 {
#   catchsql { INSERT INTO child7 VALUES(3) }
# } {1 {foreign key mismatch - "child7" referencing "parent"}}

# #-------------------------------------------------------------------------
# # Test errors in the database schema that are detected while preparing
# # DML statements. The error text for these messages always matches 
# # either "foreign key mismatch" or "no such table*" (using [string match]).
# #
# # EVIDENCE-OF: R-45488-08504 If the database schema contains foreign key
# # errors that require looking at more than one table definition to
# # identify, then those errors are not detected when the tables are
# # created.
# #
# # EVIDENCE-OF: R-48391-38472 Instead, such errors prevent the
# # application from preparing SQL statements that modify the content of
# # the child or parent tables in ways that use the foreign keys.
# #
# # EVIDENCE-OF: R-03108-63659 The English language error message for
# # foreign key DML errors is usually "foreign key mismatch" but can also
# # be "no such table" if the parent table does not exist.
# #
# # EVIDENCE-OF: R-60781-26576 Foreign key DML errors are may be reported
# # if: The parent table does not exist, or The parent key columns named
# # in the foreign key constraint do not exist, or The parent key columns
# # named in the foreign key constraint are not the primary key of the
# # parent table and are not subject to a unique constraint using
# # collating sequence specified in the CREATE TABLE, or The child table
# # references the primary key of the parent without specifying the
# # primary key columns and the number of primary key columns in the
# # parent do not match the number of child key columns.
# #
# do_test e_fkey-20.1 {
#   execsql {
#     CREATE TABLE c1(c REFERENCES nosuchtable, d);

#     CREATE TABLE p2(a, b, UNIQUE(a, b));
#     CREATE TABLE c2(c, d, FOREIGN KEY(c, d) REFERENCES p2(a, x));

#     CREATE TABLE p3(a PRIMARY KEY, b);
#     CREATE TABLE c3(c REFERENCES p3(b), d);

#     CREATE TABLE p4(a PRIMARY KEY, b);
#     CREATE UNIQUE INDEX p4i ON p4(b COLLATE nocase);
#     CREATE TABLE c4(c REFERENCES p4(b), d);

#     CREATE TABLE p5(a PRIMARY KEY, b COLLATE nocase);
#     CREATE UNIQUE INDEX p5i ON p5(b COLLATE binary);
#     CREATE TABLE c5(c REFERENCES p5(b), d);

#     CREATE TABLE p6(a PRIMARY KEY, b);
#     CREATE TABLE c6(c, d, FOREIGN KEY(c, d) REFERENCES p6);

#     CREATE TABLE p7(a, b, PRIMARY KEY(a, b));
#     CREATE TABLE c7(c, d REFERENCES p7);
#   }
# } {}

# foreach {tn tbl ptbl err} {
#   2 c1 {} "no such table: main.nosuchtable"
#   3 c2 p2 "foreign key mismatch - \"c2\" referencing \"p2\""
#   4 c3 p3 "foreign key mismatch - \"c3\" referencing \"p3\""
#   5 c4 p4 "foreign key mismatch - \"c4\" referencing \"p4\""
#   6 c5 p5 "foreign key mismatch - \"c5\" referencing \"p5\""
#   7 c6 p6 "foreign key mismatch - \"c6\" referencing \"p6\""
#   8 c7 p7 "foreign key mismatch - \"c7\" referencing \"p7\""
# } {
#   do_test e_fkey-20.$tn.1 {
#     catchsql "INSERT INTO $tbl VALUES('a', 'b')"
#   } [list 1 $err]
#   do_test e_fkey-20.$tn.2 {
#     catchsql "UPDATE $tbl SET c = ?, d = ?"
#   } [list 1 $err]
#   do_test e_fkey-20.$tn.3 {
#     catchsql "INSERT INTO $tbl SELECT ?, ?"
#   } [list 1 $err]

#   if {$ptbl ne ""} {
#     do_test e_fkey-20.$tn.4 {
#       catchsql "DELETE FROM $ptbl"
#     } [list 1 $err]
#     do_test e_fkey-20.$tn.5 {
#       catchsql "UPDATE $ptbl SET a = ?, b = ?"
#     } [list 1 $err]
#     do_test e_fkey-20.$tn.6 {
#       catchsql "INSERT INTO $ptbl SELECT ?, ?"
#     } [list 1 $err]
#   }
# }

# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-19353-43643
# #
# # Test the example of foreign key mismatch errors caused by implicitly
# # mapping a child key to the primary key of the parent table when the
# # child key consists of a different number of columns to that primary key.
# # 
# drop_all_tables
# do_test e_fkey-21.1 {
#   execsql {
#     CREATE TABLE parent2(a, b, PRIMARY KEY(a,b));

#     CREATE TABLE child8(x, y, FOREIGN KEY(x,y) REFERENCES parent2);     -- Ok
#     CREATE TABLE child9(x REFERENCES parent2);                          -- Err
#     CREATE TABLE child10(x,y,z, FOREIGN KEY(x,y,z) REFERENCES parent2); -- Err
#   }
# } {}
# do_test e_fkey-21.2 {
#   execsql {
#     INSERT INTO parent2 VALUES('I', 'II');
#     INSERT INTO child8 VALUES('I', 'II');
#   }
# } {}
# do_test e_fkey-21.3 {
#   catchsql { INSERT INTO child9 VALUES('I') }
# } {1 {foreign key mismatch - "child9" referencing "parent2"}}
# do_test e_fkey-21.4 {
#   catchsql { INSERT INTO child9 VALUES('II') }
# } {1 {foreign key mismatch - "child9" referencing "parent2"}}
# do_test e_fkey-21.5 {
#   catchsql { INSERT INTO child9 VALUES(NULL) }
# } {1 {foreign key mismatch - "child9" referencing "parent2"}}
# do_test e_fkey-21.6 {
#   catchsql { INSERT INTO child10 VALUES('I', 'II', 'III') }
# } {1 {foreign key mismatch - "child10" referencing "parent2"}}
# do_test e_fkey-21.7 {
#   catchsql { INSERT INTO child10 VALUES(1, 2, 3) }
# } {1 {foreign key mismatch - "child10" referencing "parent2"}}
# do_test e_fkey-21.8 {
#   catchsql { INSERT INTO child10 VALUES(NULL, NULL, NULL) }
# } {1 {foreign key mismatch - "child10" referencing "parent2"}}

# #-------------------------------------------------------------------------
# # Test errors that are reported when creating the child table. 
# # Specifically:
# #
# #   * different number of child and parent key columns, and
# #   * child columns that do not exist.
# #
# # EVIDENCE-OF: R-23682-59820 By contrast, if foreign key errors can be
# # recognized simply by looking at the definition of the child table and
# # without having to consult the parent table definition, then the CREATE
# # TABLE statement for the child table fails.
# #
# # These errors are reported whether or not FK support is enabled.
# #
# # EVIDENCE-OF: R-33883-28833 Foreign key DDL errors are reported
# # regardless of whether or not foreign key constraints are enabled when
# # the table is created.
# #
# drop_all_tables
# foreach fk [list OFF ON] {
#   execsql "PRAGMA foreign_keys = $fk"
#   set i 0
#   foreach {sql error} {
#     "CREATE TABLE child1(a, b, FOREIGN KEY(a, b) REFERENCES p(c))"
#       {number of columns in foreign key does not match the number of columns in the referenced table}
#     "CREATE TABLE child2(a, b, FOREIGN KEY(a, b) REFERENCES p(c, d, e))"
#       {number of columns in foreign key does not match the number of columns in the referenced table}
#     "CREATE TABLE child2(a, b, FOREIGN KEY(a, c) REFERENCES p(c, d))"
#       {unknown column "c" in foreign key definition}
#     "CREATE TABLE child2(a, b, FOREIGN KEY(c, b) REFERENCES p(c, d))"
#       {unknown column "c" in foreign key definition}
#   } {
#     do_test e_fkey-22.$fk.[incr i] {
#       catchsql $sql
#     } [list 1 $error]
#   }
# }

# #-------------------------------------------------------------------------
# # Test that a REFERENCING clause that does not specify parent key columns
# # implicitly maps to the primary key of the parent table.
# #
# # EVIDENCE-OF: R-43879-08025 Attaching a "REFERENCES <parent-table>"
# # clause to a column definition creates a foreign
# # key constraint that maps the column to the primary key of
# # <parent-table>.
# # 
# do_test e_fkey-23.1 {
#   execsql {
#     CREATE TABLE p1(a, b, PRIMARY KEY(a, b));
#     CREATE TABLE p2(a, b PRIMARY KEY);
#     CREATE TABLE c1(c, d, FOREIGN KEY(c, d) REFERENCES p1);
#     CREATE TABLE c2(a, b REFERENCES p2);
#   }
# } {}
# proc test_efkey_60 {tn isError sql} {
#   do_test e_fkey-23.$tn "
#     catchsql {$sql}
#   " [lindex {{0 {}} {1 {FOREIGN KEY constraint failed}}} $isError]
# }

# test_efkey_60 2 1 "INSERT INTO c1 VALUES(239, 231)"
# test_efkey_60 3 0 "INSERT INTO p1 VALUES(239, 231)"
# test_efkey_60 4 0 "INSERT INTO c1 VALUES(239, 231)"
# test_efkey_60 5 1 "INSERT INTO c2 VALUES(239, 231)"
# test_efkey_60 6 0 "INSERT INTO p2 VALUES(239, 231)"
# test_efkey_60 7 0 "INSERT INTO c2 VALUES(239, 231)"

# #-------------------------------------------------------------------------
# # Test that an index on on the child key columns of an FK constraint
# # is optional.
# #
# # EVIDENCE-OF: R-15417-28014 Indices are not required for child key
# # columns
# #
# # Also test that if an index is created on the child key columns, it does
# # not make a difference whether or not it is a UNIQUE index.
# #
# # EVIDENCE-OF: R-15741-50893 The child key index does not have to be
# # (and usually will not be) a UNIQUE index.
# #
# drop_all_tables
# do_test e_fkey-24.1 {
#   execsql {
#     CREATE TABLE parent(x, y, UNIQUE(y, x));
#     CREATE TABLE c1(a, b, FOREIGN KEY(a, b) REFERENCES parent(x, y));
#     CREATE TABLE c2(a, b, FOREIGN KEY(a, b) REFERENCES parent(x, y));
#     CREATE TABLE c3(a, b, FOREIGN KEY(a, b) REFERENCES parent(x, y));
#     CREATE INDEX c2i ON c2(a, b);
#     CREATE UNIQUE INDEX c3i ON c2(b, a);
#   }
# } {}
# proc test_efkey_61 {tn isError sql} {
#   do_test e_fkey-24.$tn "
#     catchsql {$sql}
#   " [lindex {{0 {}} {1 {FOREIGN KEY constraint failed}}} $isError]
# }
# foreach {tn c} [list 2 c1 3 c2 4 c3] {
#   test_efkey_61 $tn.1 1 "INSERT INTO $c VALUES(1, 2)"
#   test_efkey_61 $tn.2 0 "INSERT INTO parent VALUES(1, 2)"
#   test_efkey_61 $tn.3 0 "INSERT INTO $c VALUES(1, 2)"

#   execsql "DELETE FROM $c ; DELETE FROM parent"
# }

# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-00279-52283
# #
# # Test an example showing that when a row is deleted from the parent 
# # table, the child table is queried for orphaned rows as follows:
# #
# #   SELECT rowid FROM track WHERE trackartist = ?
# #
# # EVIDENCE-OF: R-23302-30956 If this SELECT returns any rows at all,
# # then SQLite concludes that deleting the row from the parent table
# # would violate the foreign key constraint and returns an error.
# #
# do_test e_fkey-25.1 {
#   execsql {
#     CREATE TABLE artist(
#       artistid    INTEGER PRIMARY KEY, 
#       artistname  TEXT
#     );
#     CREATE TABLE track(
#       trackid     INTEGER, 
#       trackname   TEXT, 
#       trackartist INTEGER,
#       FOREIGN KEY(trackartist) REFERENCES artist(artistid)
#     );
#   }
# } {}
# do_execsql_test e_fkey-25.2 {
#   PRAGMA foreign_keys = OFF;
#   EXPLAIN QUERY PLAN DELETE FROM artist WHERE 1;
#   EXPLAIN QUERY PLAN SELECT rowid FROM track WHERE trackartist = ?;
# } {
#   0 0 0 {SCAN TABLE artist} 
#   0 0 0 {SCAN TABLE track}
# }
# do_execsql_test e_fkey-25.3 {
#   PRAGMA foreign_keys = ON;
#   EXPLAIN QUERY PLAN DELETE FROM artist WHERE 1;
# } {
#   0 0 0 {SCAN TABLE artist} 
#   0 0 0 {SCAN TABLE track}
# }
# do_test e_fkey-25.4 {
#   execsql {
#     INSERT INTO artist VALUES(5, 'artist 5');
#     INSERT INTO artist VALUES(6, 'artist 6');
#     INSERT INTO artist VALUES(7, 'artist 7');
#     INSERT INTO track VALUES(1, 'track 1', 5);
#     INSERT INTO track VALUES(2, 'track 2', 6);
#   }
# } {}

# do_test e_fkey-25.5 {
#   concat \
#     [execsql { SELECT rowid FROM track WHERE trackartist = 5 }]   \
#     [catchsql { DELETE FROM artist WHERE artistid = 5 }]
# } {1 1 {FOREIGN KEY constraint failed}}

# do_test e_fkey-25.6 {
#   concat \
#     [execsql { SELECT rowid FROM track WHERE trackartist = 7 }]   \
#     [catchsql { DELETE FROM artist WHERE artistid = 7 }]
# } {0 {}}

# do_test e_fkey-25.7 {
#   concat \
#     [execsql { SELECT rowid FROM track WHERE trackartist = 6 }]   \
#     [catchsql { DELETE FROM artist WHERE artistid = 6 }]
# } {2 1 {FOREIGN KEY constraint failed}}

# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-47936-10044 Or, more generally:
# # SELECT rowid FROM <child-table> WHERE <child-key> = :parent_key_value
# #
# # Test that when a row is deleted from the parent table of an FK 
# # constraint, the child table is queried for orphaned rows. The
# # query is equivalent to:
# #
# #   SELECT rowid FROM <child-table> WHERE <child-key> = :parent_key_value
# #
# # Also test that when a row is inserted into the parent table, or when the 
# # parent key values of an existing row are modified, a query equivalent
# # to the following is planned. In some cases it is not executed, but it
# # is always planned.
# #
# #   SELECT rowid FROM <child-table> WHERE <child-key> = :parent_key_value
# #
# # EVIDENCE-OF: R-61616-46700 Similar queries may be run if the content
# # of the parent key is modified or a new row is inserted into the parent
# # table.
# #
# #
# drop_all_tables
# do_test e_fkey-26.1 {
#   execsql { CREATE TABLE parent(x, y, UNIQUE(y, x)) }
# } {}
# foreach {tn sql} {
#   2 { 
#     CREATE TABLE child(a, b, FOREIGN KEY(a, b) REFERENCES parent(x, y))
#   }
#   3 { 
#     CREATE TABLE child(a, b, FOREIGN KEY(a, b) REFERENCES parent(x, y));
#     CREATE INDEX childi ON child(a, b);
#   }
#   4 { 
#     CREATE TABLE child(a, b, FOREIGN KEY(a, b) REFERENCES parent(x, y));
#     CREATE UNIQUE INDEX childi ON child(b, a);
#   }
# } {
#   execsql $sql

#   execsql {PRAGMA foreign_keys = OFF}
#   set delete [concat \
#       [eqp "DELETE FROM parent WHERE 1"] \
#       [eqp "SELECT rowid FROM child WHERE a = ? AND b = ?"]
#   ]
#   set update [concat \
#       [eqp "UPDATE parent SET x=?, y=?"] \
#       [eqp "SELECT rowid FROM child WHERE a = ? AND b = ?"] \
#       [eqp "SELECT rowid FROM child WHERE a = ? AND b = ?"]
#   ]
#   execsql {PRAGMA foreign_keys = ON}

#   do_test e_fkey-26.$tn.1 { eqp "DELETE FROM parent WHERE 1" } $delete
#   do_test e_fkey-26.$tn.2 { eqp "UPDATE parent set x=?, y=?" } $update

#   execsql {DROP TABLE child}
# }

# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-14553-34013
# #
# # Test the example schema at the end of section 3. Also test that is
# # is "efficient". In this case "efficient" means that foreign key
# # related operations on the parent table do not provoke linear scans.
# #
# drop_all_tables
# do_test e_fkey-27.1 {
#   execsql {
#     CREATE TABLE artist(
#       artistid    INTEGER PRIMARY KEY, 
#       artistname  TEXT
#     );
#     CREATE TABLE track(
#       trackid     INTEGER,
#       trackname   TEXT, 
#       trackartist INTEGER REFERENCES artist
#     );
#     CREATE INDEX trackindex ON track(trackartist);
#   }
# } {}
# do_test e_fkey-27.2 {
#   eqp { INSERT INTO artist VALUES(?, ?) }
# } {}
# do_execsql_test e_fkey-27.3 {
#   EXPLAIN QUERY PLAN UPDATE artist SET artistid = ?, artistname = ?
# } {
#   0 0 0 {SCAN TABLE artist} 
#   0 0 0 {SEARCH TABLE track USING COVERING INDEX trackindex (trackartist=?)} 
#   0 0 0 {SEARCH TABLE track USING COVERING INDEX trackindex (trackartist=?)}
# }
# do_execsql_test e_fkey-27.4 {
#   EXPLAIN QUERY PLAN DELETE FROM artist
# } {
#   0 0 0 {SCAN TABLE artist} 
#   0 0 0 {SEARCH TABLE track USING COVERING INDEX trackindex (trackartist=?)}
# }


# ###########################################################################
# ### SECTION 4.1: Composite Foreign Key Constraints
# ###########################################################################

# #-------------------------------------------------------------------------
# # Check that parent and child keys must have the same number of columns.
# #
# # EVIDENCE-OF: R-41062-34431 Parent and child keys must have the same
# # cardinality.
# #
# foreach {tn sql err} {
#   1 "CREATE TABLE c(jj REFERENCES p(x, y))" 
#     {foreign key on jj should reference only one column of table p}

#   2 "CREATE TABLE c(jj REFERENCES p())" {near ")": syntax error}

#   3 "CREATE TABLE c(jj, FOREIGN KEY(jj) REFERENCES p(x, y))" 
#     {number of columns in foreign key does not match the number of columns in the referenced table}

#   4 "CREATE TABLE c(jj, FOREIGN KEY(jj) REFERENCES p())" 
#     {near ")": syntax error}

#   5 "CREATE TABLE c(ii, jj, FOREIGN KEY(jj, ii) REFERENCES p())" 
#     {near ")": syntax error}

#   6 "CREATE TABLE c(ii, jj, FOREIGN KEY(jj, ii) REFERENCES p(x))" 
#     {number of columns in foreign key does not match the number of columns in the referenced table}

#   7 "CREATE TABLE c(ii, jj, FOREIGN KEY(jj, ii) REFERENCES p(x,y,z))" 
#     {number of columns in foreign key does not match the number of columns in the referenced table}
# } {
#   drop_all_tables
#   do_test e_fkey-28.$tn [list catchsql $sql] [list 1 $err]
# }
# do_test e_fkey-28.8 {
#   drop_all_tables
#   execsql {
#     CREATE TABLE p(x PRIMARY KEY);
#     CREATE TABLE c(a, b, FOREIGN KEY(a,b) REFERENCES p);
#   }
#   catchsql {DELETE FROM p}
# } {1 {foreign key mismatch - "c" referencing "p"}}
# do_test e_fkey-28.9 {
#   drop_all_tables
#   execsql {
#     CREATE TABLE p(x, y, PRIMARY KEY(x,y));
#     CREATE TABLE c(a REFERENCES p);
#   }
#   catchsql {DELETE FROM p}
# } {1 {foreign key mismatch - "c" referencing "p"}}


# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-24676-09859
# #
# # Test the example schema in the "Composite Foreign Key Constraints" 
# # section.
# #
# do_test e_fkey-29.1 {
#   execsql {
#     CREATE TABLE album(
#       albumartist TEXT,
#       albumname TEXT,
#       albumcover BINARY,
#       PRIMARY KEY(albumartist, albumname)
#     );
#     CREATE TABLE song(
#       songid INTEGER,
#       songartist TEXT,
#       songalbum TEXT,
#       songname TEXT,
#       FOREIGN KEY(songartist, songalbum) REFERENCES album(albumartist,albumname)
#     );
#   }
# } {}

# do_test e_fkey-29.2 {
#   execsql {
#     INSERT INTO album VALUES('Elvis Presley', 'Elvis'' Christmas Album', NULL);
#     INSERT INTO song VALUES(
#       1, 'Elvis Presley', 'Elvis'' Christmas Album', 'Here Comes Santa Clause'
#     );
#   }
# } {}
# do_test e_fkey-29.3 {
#   catchsql {
#     INSERT INTO song VALUES(2, 'Elvis Presley', 'Elvis Is Back!', 'Fever');
#   }
# } {1 {FOREIGN KEY constraint failed}}


# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-33626-48418 In SQLite, if any of the child key columns
# # (in this case songartist and songalbum) are NULL, then there is no
# # requirement for a corresponding row in the parent table.
# #
# do_test e_fkey-30.1 {
#   execsql {
#     INSERT INTO song VALUES(2, 'Elvis Presley', NULL, 'Fever');
#     INSERT INTO song VALUES(3, NULL, 'Elvis Is Back', 'Soldier Boy');
#   }
# } {}

# ###########################################################################
# ### SECTION 4.2: Deferred Foreign Key Constraints
# ###########################################################################

# #-------------------------------------------------------------------------
# # Test that if a statement violates an immediate FK constraint, and the
# # database does not satisfy the FK constraint once all effects of the
# # statement have been applied, an error is reported and the effects of
# # the statement rolled back.
# #
# # EVIDENCE-OF: R-09323-30470 If a statement modifies the contents of the
# # database so that an immediate foreign key constraint is in violation
# # at the conclusion the statement, an exception is thrown and the
# # effects of the statement are reverted.
# #
# drop_all_tables
# do_test e_fkey-31.1 {
#   execsql {
#     CREATE TABLE king(a, b, PRIMARY KEY(a));
#     CREATE TABLE prince(c REFERENCES king, d);
#   }
# } {}

# do_test e_fkey-31.2 {
#   # Execute a statement that violates the immediate FK constraint.
#   catchsql { INSERT INTO prince VALUES(1, 2) }
# } {1 {FOREIGN KEY constraint failed}}

# do_test e_fkey-31.3 {
#   # This time, use a trigger to fix the constraint violation before the
#   # statement has finished executing. Then execute the same statement as
#   # in the previous test case. This time, no error.
#   execsql {
#     CREATE TRIGGER kt AFTER INSERT ON prince WHEN
#       NOT EXISTS (SELECT a FROM king WHERE a = new.c)
#     BEGIN
#       INSERT INTO king VALUES(new.c, NULL);
#     END
#   }
#   execsql { INSERT INTO prince VALUES(1, 2) }
# } {}

# # Test that operating inside a transaction makes no difference to 
# # immediate constraint violation handling.
# do_test e_fkey-31.4 {
#   execsql {
#     BEGIN;
#     INSERT INTO prince VALUES(2, 3);
#     DROP TRIGGER kt;
#   }
#   catchsql { INSERT INTO prince VALUES(3, 4) }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-31.5 {
#   execsql {
#     COMMIT;
#     SELECT * FROM king;
#   }
# } {1 {} 2 {}}

# #-------------------------------------------------------------------------
# # Test that if a deferred constraint is violated within a transaction,
# # nothing happens immediately and the database is allowed to persist
# # in a state that does not satisfy the FK constraint. However attempts
# # to COMMIT the transaction fail until the FK constraint is satisfied.
# #
# # EVIDENCE-OF: R-49178-21358 By contrast, if a statement modifies the
# # contents of the database such that a deferred foreign key constraint
# # is violated, the violation is not reported immediately.
# #
# # EVIDENCE-OF: R-39692-12488 Deferred foreign key constraints are not
# # checked until the transaction tries to COMMIT.
# #
# # EVIDENCE-OF: R-55147-47664 For as long as the user has an open
# # transaction, the database is allowed to exist in a state that violates
# # any number of deferred foreign key constraints.
# #
# # EVIDENCE-OF: R-29604-30395 However, COMMIT will fail as long as
# # foreign key constraints remain in violation.
# #
# proc test_efkey_34 {tn isError sql} {
#   do_test e_fkey-32.$tn "
#     catchsql {$sql}
#   " [lindex {{0 {}} {1 {FOREIGN KEY constraint failed}}} $isError]
# }
# drop_all_tables

# test_efkey_34  1 0 {
#   CREATE TABLE ll(k PRIMARY KEY);
#   CREATE TABLE kk(c REFERENCES ll DEFERRABLE INITIALLY DEFERRED);
# }
# test_efkey_34  2 0 "BEGIN"
# test_efkey_34  3 0   "INSERT INTO kk VALUES(5)"
# test_efkey_34  4 0   "INSERT INTO kk VALUES(10)"
# test_efkey_34  5 1 "COMMIT"
# test_efkey_34  6 0   "INSERT INTO ll VALUES(10)"
# test_efkey_34  7 1 "COMMIT"
# test_efkey_34  8 0   "INSERT INTO ll VALUES(5)"
# test_efkey_34  9 0 "COMMIT"

# #-------------------------------------------------------------------------
# # When not running inside a transaction, a deferred constraint is similar
# # to an immediate constraint (violations are reported immediately).
# #
# # EVIDENCE-OF: R-56844-61705 If the current statement is not inside an
# # explicit transaction (a BEGIN/COMMIT/ROLLBACK block), then an implicit
# # transaction is committed as soon as the statement has finished
# # executing. In this case deferred constraints behave the same as
# # immediate constraints.
# #
# drop_all_tables
# proc test_efkey_35 {tn isError sql} {
#   do_test e_fkey-33.$tn "
#     catchsql {$sql}
#   " [lindex {{0 {}} {1 {FOREIGN KEY constraint failed}}} $isError]
# }
# do_test e_fkey-33.1 {
#   execsql {
#     CREATE TABLE parent(x, y);
#     CREATE UNIQUE INDEX pi ON parent(x, y);
#     CREATE TABLE child(a, b,
#       FOREIGN KEY(a, b) REFERENCES parent(x, y) DEFERRABLE INITIALLY DEFERRED
#     );
#   }
# } {}
# test_efkey_35 2 1 "INSERT INTO child  VALUES('x', 'y')"
# test_efkey_35 3 0 "INSERT INTO parent VALUES('x', 'y')"
# test_efkey_35 4 0 "INSERT INTO child  VALUES('x', 'y')"


# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-12782-61841
# #
# # Test that an FK constraint is made deferred by adding the following
# # to the definition:
# #
# #   DEFERRABLE INITIALLY DEFERRED
# #
# # EVIDENCE-OF: R-09005-28791
# #
# # Also test that adding any of the following to a foreign key definition 
# # makes the constraint IMMEDIATE:
# #
# #   NOT DEFERRABLE INITIALLY DEFERRED
# #   NOT DEFERRABLE INITIALLY IMMEDIATE
# #   NOT DEFERRABLE
# #   DEFERRABLE INITIALLY IMMEDIATE
# #   DEFERRABLE
# #
# # Foreign keys are IMMEDIATE by default (if there is no DEFERRABLE or NOT
# # DEFERRABLE clause).
# #
# # EVIDENCE-OF: R-35290-16460 Foreign key constraints are immediate by
# # default.
# #
# # EVIDENCE-OF: R-30323-21917 Each foreign key constraint in SQLite is
# # classified as either immediate or deferred.
# #
# drop_all_tables
# do_test e_fkey-34.1 {
#   execsql {
#     CREATE TABLE parent(x, y, z, PRIMARY KEY(x,y,z));
#     CREATE TABLE c1(a, b, c,
#       FOREIGN KEY(a, b, c) REFERENCES parent NOT DEFERRABLE INITIALLY DEFERRED
#     );
#     CREATE TABLE c2(a, b, c,
#       FOREIGN KEY(a, b, c) REFERENCES parent NOT DEFERRABLE INITIALLY IMMEDIATE
#     );
#     CREATE TABLE c3(a, b, c,
#       FOREIGN KEY(a, b, c) REFERENCES parent NOT DEFERRABLE
#     );
#     CREATE TABLE c4(a, b, c,
#       FOREIGN KEY(a, b, c) REFERENCES parent DEFERRABLE INITIALLY IMMEDIATE
#     );
#     CREATE TABLE c5(a, b, c,
#       FOREIGN KEY(a, b, c) REFERENCES parent DEFERRABLE
#     );
#     CREATE TABLE c6(a, b, c, FOREIGN KEY(a, b, c) REFERENCES parent);

#     -- This FK constraint is the only deferrable one.
#     CREATE TABLE c7(a, b, c,
#       FOREIGN KEY(a, b, c) REFERENCES parent DEFERRABLE INITIALLY DEFERRED
#     );

#     INSERT INTO parent VALUES('a', 'b', 'c');
#     INSERT INTO parent VALUES('d', 'e', 'f');
#     INSERT INTO parent VALUES('g', 'h', 'i');
#     INSERT INTO parent VALUES('j', 'k', 'l');
#     INSERT INTO parent VALUES('m', 'n', 'o');
#     INSERT INTO parent VALUES('p', 'q', 'r');
#     INSERT INTO parent VALUES('s', 't', 'u');

#     INSERT INTO c1 VALUES('a', 'b', 'c');
#     INSERT INTO c2 VALUES('d', 'e', 'f');
#     INSERT INTO c3 VALUES('g', 'h', 'i');
#     INSERT INTO c4 VALUES('j', 'k', 'l');
#     INSERT INTO c5 VALUES('m', 'n', 'o');
#     INSERT INTO c6 VALUES('p', 'q', 'r');
#     INSERT INTO c7 VALUES('s', 't', 'u');
#   }
# } {}

# proc test_efkey_29 {tn sql isError} {
#   do_test e_fkey-34.$tn "catchsql {$sql}" [
#     lindex {{0 {}} {1 {FOREIGN KEY constraint failed}}} $isError
#   ]
# }
# test_efkey_29  2 "BEGIN"                                   0
# test_efkey_29  3 "DELETE FROM parent WHERE x = 'a'"        1
# test_efkey_29  4 "DELETE FROM parent WHERE x = 'd'"        1
# test_efkey_29  5 "DELETE FROM parent WHERE x = 'g'"        1
# test_efkey_29  6 "DELETE FROM parent WHERE x = 'j'"        1
# test_efkey_29  7 "DELETE FROM parent WHERE x = 'm'"        1
# test_efkey_29  8 "DELETE FROM parent WHERE x = 'p'"        1
# test_efkey_29  9 "DELETE FROM parent WHERE x = 's'"        0
# test_efkey_29 10 "COMMIT"                                  1
# test_efkey_29 11 "ROLLBACK"                                0

# test_efkey_29  9 "BEGIN"                                   0
# test_efkey_29 10 "UPDATE parent SET z = 'z' WHERE z = 'c'" 1
# test_efkey_29 11 "UPDATE parent SET z = 'z' WHERE z = 'f'" 1
# test_efkey_29 12 "UPDATE parent SET z = 'z' WHERE z = 'i'" 1
# test_efkey_29 13 "UPDATE parent SET z = 'z' WHERE z = 'l'" 1
# test_efkey_29 14 "UPDATE parent SET z = 'z' WHERE z = 'o'" 1
# test_efkey_29 15 "UPDATE parent SET z = 'z' WHERE z = 'r'" 1
# test_efkey_29 16 "UPDATE parent SET z = 'z' WHERE z = 'u'" 0
# test_efkey_29 17 "COMMIT"                                  1
# test_efkey_29 18 "ROLLBACK"                                0

# test_efkey_29 17 "BEGIN"                                   0
# test_efkey_29 18 "INSERT INTO c1 VALUES(1, 2, 3)"          1
# test_efkey_29 19 "INSERT INTO c2 VALUES(1, 2, 3)"          1
# test_efkey_29 20 "INSERT INTO c3 VALUES(1, 2, 3)"          1
# test_efkey_29 21 "INSERT INTO c4 VALUES(1, 2, 3)"          1
# test_efkey_29 22 "INSERT INTO c5 VALUES(1, 2, 3)"          1
# test_efkey_29 22 "INSERT INTO c6 VALUES(1, 2, 3)"          1
# test_efkey_29 22 "INSERT INTO c7 VALUES(1, 2, 3)"          0
# test_efkey_29 23 "COMMIT"                                  1
# test_efkey_29 24 "INSERT INTO parent VALUES(1, 2, 3)"      0
# test_efkey_29 25 "COMMIT"                                  0

# test_efkey_29 26 "BEGIN"                                   0
# test_efkey_29 27 "UPDATE c1 SET a = 10"                    1
# test_efkey_29 28 "UPDATE c2 SET a = 10"                    1
# test_efkey_29 29 "UPDATE c3 SET a = 10"                    1
# test_efkey_29 30 "UPDATE c4 SET a = 10"                    1
# test_efkey_29 31 "UPDATE c5 SET a = 10"                    1
# test_efkey_29 31 "UPDATE c6 SET a = 10"                    1
# test_efkey_29 31 "UPDATE c7 SET a = 10"                    0
# test_efkey_29 32 "COMMIT"                                  1
# test_efkey_29 33 "ROLLBACK"                                0

# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-24499-57071
# #
# # Test an example from foreignkeys.html dealing with a deferred foreign 
# # key constraint.
# #
# do_test e_fkey-35.1 {
#   drop_all_tables
#   execsql {
#     CREATE TABLE artist(
#       artistid    INTEGER PRIMARY KEY, 
#       artistname  TEXT
#     );
#     CREATE TABLE track(
#       trackid     INTEGER,
#       trackname   TEXT, 
#       trackartist INTEGER REFERENCES artist(artistid) DEFERRABLE INITIALLY DEFERRED
#     );
#   }
# } {}
# do_test e_fkey-35.2 {
#   execsql {
#     BEGIN;
#       INSERT INTO track VALUES(1, 'White Christmas', 5);
#   }
#   catchsql COMMIT
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-35.3 {
#   execsql {
#     INSERT INTO artist VALUES(5, 'Bing Crosby');
#     COMMIT;
#   }
# } {}

# #-------------------------------------------------------------------------
# # Verify that a nested savepoint may be released without satisfying 
# # deferred foreign key constraints.
# #
# # EVIDENCE-OF: R-07223-48323 A nested savepoint transaction may be
# # RELEASEd while the database is in a state that does not satisfy a
# # deferred foreign key constraint.
# #
# drop_all_tables
# do_test e_fkey-36.1 {
#   execsql {
#     CREATE TABLE t1(a PRIMARY KEY,
#       b REFERENCES t1 DEFERRABLE INITIALLY DEFERRED
#     );
#     INSERT INTO t1 VALUES(1, 1);
#     INSERT INTO t1 VALUES(2, 2);
#     INSERT INTO t1 VALUES(3, 3);
#   }
# } {}
# do_test e_fkey-36.2 {
#   execsql {
#     BEGIN;
#       SAVEPOINT one;
#         INSERT INTO t1 VALUES(4, 5);
#       RELEASE one;
#   }
# } {}
# do_test e_fkey-36.3 {
#   catchsql COMMIT
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-36.4 {
#   execsql {
#     UPDATE t1 SET a = 5 WHERE a = 4;
#     COMMIT;
#   }
# } {}


# #-------------------------------------------------------------------------
# # Check that a transaction savepoint (an outermost savepoint opened when
# # the database was in auto-commit mode) cannot be released without
# # satisfying deferred foreign key constraints. It may be rolled back.
# #
# # EVIDENCE-OF: R-44295-13823 A transaction savepoint (a non-nested
# # savepoint that was opened while there was not currently an open
# # transaction), on the other hand, is subject to the same restrictions
# # as a COMMIT - attempting to RELEASE it while the database is in such a
# # state will fail.
# #
# do_test e_fkey-37.1 {
#   execsql {
#     SAVEPOINT one;
#       SAVEPOINT two;
#         INSERT INTO t1 VALUES(6, 7);
#       RELEASE two;
#   }
# } {}
# do_test e_fkey-37.2 {
#   catchsql {RELEASE one}
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-37.3 {
#   execsql {
#       UPDATE t1 SET a = 7 WHERE a = 6;
#     RELEASE one;
#   }
# } {}
# do_test e_fkey-37.4 {
#   execsql {
#     SAVEPOINT one;
#       SAVEPOINT two;
#         INSERT INTO t1 VALUES(9, 10);
#       RELEASE two;
#   }
# } {}
# do_test e_fkey-37.5 {
#   catchsql {RELEASE one}
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-37.6 {
#   execsql {ROLLBACK TO one ; RELEASE one}
# } {}

# #-------------------------------------------------------------------------
# # Test that if a COMMIT operation fails due to deferred foreign key 
# # constraints, any nested savepoints remain open.
# #
# # EVIDENCE-OF: R-37736-42616 If a COMMIT statement (or the RELEASE of a
# # transaction SAVEPOINT) fails because the database is currently in a
# # state that violates a deferred foreign key constraint and there are
# # currently nested savepoints, the nested savepoints remain open.
# #
# do_test e_fkey-38.1 {
#   execsql {
#     DELETE FROM t1 WHERE a>3;
#     SELECT * FROM t1;
#   }
# } {1 1 2 2 3 3}
# do_test e_fkey-38.2 {
#   execsql {
#     BEGIN;
#       INSERT INTO t1 VALUES(4, 4);
#       SAVEPOINT one;
#         INSERT INTO t1 VALUES(5, 6);
#         SELECT * FROM t1;
#   }
# } {1 1 2 2 3 3 4 4 5 6}
# do_test e_fkey-38.3 {
#   catchsql COMMIT
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-38.4 {
#   execsql {
#     ROLLBACK TO one;
#     COMMIT;
#     SELECT * FROM t1;
#   }
# } {1 1 2 2 3 3 4 4}

# do_test e_fkey-38.5 {
#   execsql {
#     SAVEPOINT a;
#       INSERT INTO t1 VALUES(5, 5);
#       SAVEPOINT b;
#         INSERT INTO t1 VALUES(6, 7);
#         SAVEPOINT c;
#           INSERT INTO t1 VALUES(7, 8);
#   }
# } {}
# do_test e_fkey-38.6 {
#   catchsql {RELEASE a}
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-38.7 {
#   execsql  {ROLLBACK TO c}
#   catchsql {RELEASE a}
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-38.8 {
#   execsql  {
#     ROLLBACK TO b;
#     RELEASE a;
#     SELECT * FROM t1;
#   }
# } {1 1 2 2 3 3 4 4 5 5}

# ###########################################################################
# ### SECTION 4.3: ON DELETE and ON UPDATE Actions
# ###########################################################################

# #-------------------------------------------------------------------------
# # Test that configured ON DELETE and ON UPDATE actions take place when
# # deleting or modifying rows of the parent table, respectively.
# #
# # EVIDENCE-OF: R-48270-44282 Foreign key ON DELETE and ON UPDATE clauses
# # are used to configure actions that take place when deleting rows from
# # the parent table (ON DELETE), or modifying the parent key values of
# # existing rows (ON UPDATE).
# #
# # Test that a single FK constraint may have different actions configured
# # for ON DELETE and ON UPDATE.
# #
# # EVIDENCE-OF: R-48124-63225 A single foreign key constraint may have
# # different actions configured for ON DELETE and ON UPDATE.
# #
# do_test e_fkey-39.1 {
#   execsql {
#     CREATE TABLE p(a, b PRIMARY KEY, c);
#     CREATE TABLE c1(d, e, f DEFAULT 'k0' REFERENCES p 
#       ON UPDATE SET DEFAULT
#       ON DELETE SET NULL
#     );

#     INSERT INTO p VALUES(0, 'k0', '');
#     INSERT INTO p VALUES(1, 'k1', 'I');
#     INSERT INTO p VALUES(2, 'k2', 'II');
#     INSERT INTO p VALUES(3, 'k3', 'III');

#     INSERT INTO c1 VALUES(1, 'xx', 'k1');
#     INSERT INTO c1 VALUES(2, 'xx', 'k2');
#     INSERT INTO c1 VALUES(3, 'xx', 'k3');
#   }
# } {}
# do_test e_fkey-39.2 {
#   execsql {
#     UPDATE p SET b = 'k4' WHERE a = 1;
#     SELECT * FROM c1;
#   }
# } {1 xx k0 2 xx k2 3 xx k3}
# do_test e_fkey-39.3 {
#   execsql {
#     DELETE FROM p WHERE a = 2;
#     SELECT * FROM c1;
#   }
# } {1 xx k0 2 xx {} 3 xx k3}
# do_test e_fkey-39.4 {
#   execsql {
#     CREATE UNIQUE INDEX pi ON p(c);
#     REPLACE INTO p VALUES(5, 'k5', 'III');
#     SELECT * FROM c1;
#   }
# } {1 xx k0 2 xx {} 3 xx {}}

# #-------------------------------------------------------------------------
# # Each foreign key in the system has an ON UPDATE and ON DELETE action,
# # either "NO ACTION", "RESTRICT", "SET NULL", "SET DEFAULT" or "CASCADE".
# #
# # EVIDENCE-OF: R-33326-45252 The ON DELETE and ON UPDATE action
# # associated with each foreign key in an SQLite database is one of "NO
# # ACTION", "RESTRICT", "SET NULL", "SET DEFAULT" or "CASCADE".
# #
# # If none is specified explicitly, "NO ACTION" is the default.
# #
# # EVIDENCE-OF: R-19803-45884 If an action is not explicitly specified,
# # it defaults to "NO ACTION".
# # 
# drop_all_tables
# do_test e_fkey-40.1 {
#   execsql {
#     CREATE TABLE parent(x PRIMARY KEY, y);
#     CREATE TABLE child1(a, 
#       b REFERENCES parent ON UPDATE NO ACTION ON DELETE RESTRICT
#     );
#     CREATE TABLE child2(a, 
#       b REFERENCES parent ON UPDATE RESTRICT ON DELETE SET NULL
#     );
#     CREATE TABLE child3(a, 
#       b REFERENCES parent ON UPDATE SET NULL ON DELETE SET DEFAULT
#     );
#     CREATE TABLE child4(a, 
#       b REFERENCES parent ON UPDATE SET DEFAULT ON DELETE CASCADE
#     );

#     -- Create some foreign keys that use the default action - "NO ACTION"
#     CREATE TABLE child5(a, b REFERENCES parent ON UPDATE CASCADE);
#     CREATE TABLE child6(a, b REFERENCES parent ON DELETE RESTRICT);
#     CREATE TABLE child7(a, b REFERENCES parent ON DELETE NO ACTION);
#     CREATE TABLE child8(a, b REFERENCES parent ON UPDATE NO ACTION);
#   }
# } {}

# foreach {tn zTab lRes} {
#   2 child1 {0 0 parent b {} {NO ACTION} RESTRICT NONE}
#   3 child2 {0 0 parent b {} RESTRICT {SET NULL} NONE}
#   4 child3 {0 0 parent b {} {SET NULL} {SET DEFAULT} NONE}
#   5 child4 {0 0 parent b {} {SET DEFAULT} CASCADE NONE}
#   6 child5 {0 0 parent b {} CASCADE {NO ACTION} NONE}
#   7 child6 {0 0 parent b {} {NO ACTION} RESTRICT NONE}
#   8 child7 {0 0 parent b {} {NO ACTION} {NO ACTION} NONE}
#   9 child8 {0 0 parent b {} {NO ACTION} {NO ACTION} NONE}
# } {
#   do_test e_fkey-40.$tn { execsql "PRAGMA foreign_key_list($zTab)" } $lRes
# }

# #-------------------------------------------------------------------------
# # Test that "NO ACTION" means that nothing happens to a child row when
# # it's parent row is updated or deleted.
# #
# # EVIDENCE-OF: R-19971-54976 Configuring "NO ACTION" means just that:
# # when a parent key is modified or deleted from the database, no special
# # action is taken.
# #
# drop_all_tables
# do_test e_fkey-41.1 {
#   execsql {
#     CREATE TABLE parent(p1, p2, PRIMARY KEY(p1, p2));
#     CREATE TABLE child(c1, c2, 
#       FOREIGN KEY(c1, c2) REFERENCES parent
#       ON UPDATE NO ACTION
#       ON DELETE NO ACTION
#       DEFERRABLE INITIALLY DEFERRED
#     );
#     INSERT INTO parent VALUES('j', 'k');
#     INSERT INTO parent VALUES('l', 'm');
#     INSERT INTO child VALUES('j', 'k');
#     INSERT INTO child VALUES('l', 'm');
#   }
# } {}
# do_test e_fkey-41.2 {
#   execsql {
#     BEGIN;
#       UPDATE parent SET p1='k' WHERE p1='j';
#       DELETE FROM parent WHERE p1='l';
#       SELECT * FROM child;
#   }
# } {j k l m}
# do_test e_fkey-41.3 {
#   catchsql COMMIT
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-41.4 {
#   execsql ROLLBACK
# } {}

# #-------------------------------------------------------------------------
# # Test that "RESTRICT" means the application is prohibited from deleting
# # or updating a parent table row when there exists one or more child keys
# # mapped to it.
# #
# # EVIDENCE-OF: R-04272-38653 The "RESTRICT" action means that the
# # application is prohibited from deleting (for ON DELETE RESTRICT) or
# # modifying (for ON UPDATE RESTRICT) a parent key when there exists one
# # or more child keys mapped to it.
# #
# drop_all_tables
# do_test e_fkey-41.1 {
#   execsql {
#     CREATE TABLE parent(p1, p2);
#     CREATE UNIQUE INDEX parent_i ON parent(p1, p2);
#     CREATE TABLE child1(c1, c2, 
#       FOREIGN KEY(c2, c1) REFERENCES parent(p1, p2) ON DELETE RESTRICT
#     );
#     CREATE TABLE child2(c1, c2, 
#       FOREIGN KEY(c2, c1) REFERENCES parent(p1, p2) ON UPDATE RESTRICT
#     );
#   }
# } {}
# do_test e_fkey-41.2 {
#   execsql {
#     INSERT INTO parent VALUES('a', 'b');
#     INSERT INTO parent VALUES('c', 'd');
#     INSERT INTO child1 VALUES('b', 'a');
#     INSERT INTO child2 VALUES('d', 'c');
#   }
# } {}
# do_test e_fkey-41.3 {
#   catchsql { DELETE FROM parent WHERE p1 = 'a' }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-41.4 {
#   catchsql { UPDATE parent SET p2 = 'e' WHERE p1 = 'c' }
# } {1 {FOREIGN KEY constraint failed}}

# #-------------------------------------------------------------------------
# # Test that RESTRICT is slightly different from NO ACTION for IMMEDIATE
# # constraints, in that it is enforced immediately, not at the end of the 
# # statement.
# #
# # EVIDENCE-OF: R-37997-42187 The difference between the effect of a
# # RESTRICT action and normal foreign key constraint enforcement is that
# # the RESTRICT action processing happens as soon as the field is updated
# # - not at the end of the current statement as it would with an
# # immediate constraint, or at the end of the current transaction as it
# # would with a deferred constraint.
# #
# drop_all_tables
# do_test e_fkey-42.1 {
#   execsql {
#     CREATE TABLE parent(x PRIMARY KEY);
#     CREATE TABLE child1(c REFERENCES parent ON UPDATE RESTRICT);
#     CREATE TABLE child2(c REFERENCES parent ON UPDATE NO ACTION);

#     INSERT INTO parent VALUES('key1');
#     INSERT INTO parent VALUES('key2');
#     INSERT INTO child1 VALUES('key1');
#     INSERT INTO child2 VALUES('key2');

#     CREATE TRIGGER parent_t AFTER UPDATE ON parent BEGIN
#       UPDATE child1 set c = new.x WHERE c = old.x;
#       UPDATE child2 set c = new.x WHERE c = old.x;
#     END;
#   }
# } {}
# do_test e_fkey-42.2 {
#   catchsql { UPDATE parent SET x = 'key one' WHERE x = 'key1' }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-42.3 {
#   execsql { 
#     UPDATE parent SET x = 'key two' WHERE x = 'key2';
#     SELECT * FROM child2;
#   }
# } {{key two}}

# drop_all_tables
# do_test e_fkey-42.4 {
#   execsql {
#     CREATE TABLE parent(x PRIMARY KEY);
#     CREATE TABLE child1(c REFERENCES parent ON DELETE RESTRICT);
#     CREATE TABLE child2(c REFERENCES parent ON DELETE NO ACTION);

#     INSERT INTO parent VALUES('key1');
#     INSERT INTO parent VALUES('key2');
#     INSERT INTO child1 VALUES('key1');
#     INSERT INTO child2 VALUES('key2');

#     CREATE TRIGGER parent_t AFTER DELETE ON parent BEGIN
#       UPDATE child1 SET c = NULL WHERE c = old.x;
#       UPDATE child2 SET c = NULL WHERE c = old.x;
#     END;
#   }
# } {}
# do_test e_fkey-42.5 {
#   catchsql { DELETE FROM parent WHERE x = 'key1' }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-42.6 {
#   execsql { 
#     DELETE FROM parent WHERE x = 'key2';
#     SELECT * FROM child2;
#   }
# } {{}}

# drop_all_tables
# do_test e_fkey-42.7 {
#   execsql {
#     CREATE TABLE parent(x PRIMARY KEY);
#     CREATE TABLE child1(c REFERENCES parent ON DELETE RESTRICT);
#     CREATE TABLE child2(c REFERENCES parent ON DELETE NO ACTION);

#     INSERT INTO parent VALUES('key1');
#     INSERT INTO parent VALUES('key2');
#     INSERT INTO child1 VALUES('key1');
#     INSERT INTO child2 VALUES('key2');
#   }
# } {}
# do_test e_fkey-42.8 {
#   catchsql { REPLACE INTO parent VALUES('key1') }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-42.9 {
#   execsql { 
#     REPLACE INTO parent VALUES('key2');
#     SELECT * FROM child2;
#   }
# } {key2}

# #-------------------------------------------------------------------------
# # Test that RESTRICT is enforced immediately, even for a DEFERRED constraint.
# #
# # EVIDENCE-OF: R-24179-60523 Even if the foreign key constraint it is
# # attached to is deferred, configuring a RESTRICT action causes SQLite
# # to return an error immediately if a parent key with dependent child
# # keys is deleted or modified.
# #
# drop_all_tables
# do_test e_fkey-43.1 {
#   execsql {
#     CREATE TABLE parent(x PRIMARY KEY);
#     CREATE TABLE child1(c REFERENCES parent ON UPDATE RESTRICT
#       DEFERRABLE INITIALLY DEFERRED
#     );
#     CREATE TABLE child2(c REFERENCES parent ON UPDATE NO ACTION
#       DEFERRABLE INITIALLY DEFERRED
#     );

#     INSERT INTO parent VALUES('key1');
#     INSERT INTO parent VALUES('key2');
#     INSERT INTO child1 VALUES('key1');
#     INSERT INTO child2 VALUES('key2');
#     BEGIN;
#   }
# } {}
# do_test e_fkey-43.2 {
#   catchsql { UPDATE parent SET x = 'key one' WHERE x = 'key1' }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-43.3 {
#   execsql { UPDATE parent SET x = 'key two' WHERE x = 'key2' }
# } {}
# do_test e_fkey-43.4 {
#   catchsql COMMIT
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-43.5 {
#   execsql {
#     UPDATE child2 SET c = 'key two';
#     COMMIT;
#   }
# } {}

# drop_all_tables
# do_test e_fkey-43.6 {
#   execsql {
#     CREATE TABLE parent(x PRIMARY KEY);
#     CREATE TABLE child1(c REFERENCES parent ON DELETE RESTRICT
#       DEFERRABLE INITIALLY DEFERRED
#     );
#     CREATE TABLE child2(c REFERENCES parent ON DELETE NO ACTION
#       DEFERRABLE INITIALLY DEFERRED
#     );

#     INSERT INTO parent VALUES('key1');
#     INSERT INTO parent VALUES('key2');
#     INSERT INTO child1 VALUES('key1');
#     INSERT INTO child2 VALUES('key2');
#     BEGIN;
#   }
# } {}
# do_test e_fkey-43.7 {
#   catchsql { DELETE FROM parent WHERE x = 'key1' }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-43.8 {
#   execsql { DELETE FROM parent WHERE x = 'key2' }
# } {}
# do_test e_fkey-43.9 {
#   catchsql COMMIT
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-43.10 {
#   execsql {
#     UPDATE child2 SET c = NULL;
#     COMMIT;
#   }
# } {}

# #-------------------------------------------------------------------------
# # Test SET NULL actions.
# #
# # EVIDENCE-OF: R-03353-05327 If the configured action is "SET NULL",
# # then when a parent key is deleted (for ON DELETE SET NULL) or modified
# # (for ON UPDATE SET NULL), the child key columns of all rows in the
# # child table that mapped to the parent key are set to contain SQL NULL
# # values.
# #
# drop_all_tables
# do_test e_fkey-44.1 {
#   execsql {
#     CREATE TABLE pA(x PRIMARY KEY);
#     CREATE TABLE cA(c REFERENCES pA ON DELETE SET NULL);
#     CREATE TABLE cB(c REFERENCES pA ON UPDATE SET NULL);

#     INSERT INTO pA VALUES(X'ABCD');
#     INSERT INTO pA VALUES(X'1234');
#     INSERT INTO cA VALUES(X'ABCD');
#     INSERT INTO cB VALUES(X'1234');
#   }
# } {}
# do_test e_fkey-44.2 {
#   execsql {
#     DELETE FROM pA WHERE rowid = 1;
#     SELECT quote(x) FROM pA;
#   }
# } {X'1234'}
# do_test e_fkey-44.3 {
#   execsql {
#     SELECT quote(c) FROM cA;
#   }
# } {NULL}
# do_test e_fkey-44.4 {
#   execsql {
#     UPDATE pA SET x = X'8765' WHERE rowid = 2;
#     SELECT quote(x) FROM pA;
#   }
# } {X'8765'}
# do_test e_fkey-44.5 {
#   execsql { SELECT quote(c) FROM cB }
# } {NULL}

# #-------------------------------------------------------------------------
# # Test SET DEFAULT actions.
# #
# # EVIDENCE-OF: R-43054-54832 The "SET DEFAULT" actions are similar to
# # "SET NULL", except that each of the child key columns is set to
# # contain the columns default value instead of NULL.
# #
# drop_all_tables
# do_test e_fkey-45.1 {
#   execsql {
#     CREATE TABLE pA(x PRIMARY KEY);
#     CREATE TABLE cA(c DEFAULT X'0000' REFERENCES pA ON DELETE SET DEFAULT);
#     CREATE TABLE cB(c DEFAULT X'9999' REFERENCES pA ON UPDATE SET DEFAULT);

#     INSERT INTO pA(rowid, x) VALUES(1, X'0000');
#     INSERT INTO pA(rowid, x) VALUES(2, X'9999');
#     INSERT INTO pA(rowid, x) VALUES(3, X'ABCD');
#     INSERT INTO pA(rowid, x) VALUES(4, X'1234');

#     INSERT INTO cA VALUES(X'ABCD');
#     INSERT INTO cB VALUES(X'1234');
#   }
# } {}
# do_test e_fkey-45.2 {
#   execsql {
#     DELETE FROM pA WHERE rowid = 3;
#     SELECT quote(x) FROM pA ORDER BY rowid;
#   }
# } {X'0000' X'9999' X'1234'}
# do_test e_fkey-45.3 {
#   execsql { SELECT quote(c) FROM cA }
# } {X'0000'}
# do_test e_fkey-45.4 {
#   execsql {
#     UPDATE pA SET x = X'8765' WHERE rowid = 4;
#     SELECT quote(x) FROM pA ORDER BY rowid;
#   }
# } {X'0000' X'9999' X'8765'}
# do_test e_fkey-45.5 {
#   execsql { SELECT quote(c) FROM cB }
# } {X'9999'}

# #-------------------------------------------------------------------------
# # Test ON DELETE CASCADE actions.
# #
# # EVIDENCE-OF: R-61376-57267 A "CASCADE" action propagates the delete or
# # update operation on the parent key to each dependent child key.
# #
# # EVIDENCE-OF: R-61809-62207 For an "ON DELETE CASCADE" action, this
# # means that each row in the child table that was associated with the
# # deleted parent row is also deleted.
# #
# drop_all_tables
# do_test e_fkey-46.1 {
#   execsql {
#     CREATE TABLE p1(a, b UNIQUE);
#     CREATE TABLE c1(c REFERENCES p1(b) ON DELETE CASCADE, d);
#     INSERT INTO p1 VALUES(NULL, NULL);
#     INSERT INTO p1 VALUES(4, 4);
#     INSERT INTO p1 VALUES(5, 5);
#     INSERT INTO c1 VALUES(NULL, NULL);
#     INSERT INTO c1 VALUES(4, 4);
#     INSERT INTO c1 VALUES(5, 5);
#     SELECT count(*) FROM c1;
#   }
# } {3}
# do_test e_fkey-46.2 {
#   execsql {
#     DELETE FROM p1 WHERE a = 4;
#     SELECT d, c FROM c1;
#   }
# } {{} {} 5 5}
# do_test e_fkey-46.3 {
#   execsql {
#     DELETE FROM p1;
#     SELECT d, c FROM c1;
#   }
# } {{} {}}
# do_test e_fkey-46.4 {
#   execsql { SELECT * FROM p1 }
# } {}


# #-------------------------------------------------------------------------
# # Test ON UPDATE CASCADE actions.
# #
# # EVIDENCE-OF: R-13877-64542 For an "ON UPDATE CASCADE" action, it means
# # that the values stored in each dependent child key are modified to
# # match the new parent key values.
# #
# # EVIDENCE-OF: R-61376-57267 A "CASCADE" action propagates the delete or
# # update operation on the parent key to each dependent child key.
# #
# drop_all_tables
# do_test e_fkey-47.1 {
#   execsql {
#     CREATE TABLE p1(a, b UNIQUE);
#     CREATE TABLE c1(c REFERENCES p1(b) ON UPDATE CASCADE, d);
#     INSERT INTO p1 VALUES(NULL, NULL);
#     INSERT INTO p1 VALUES(4, 4);
#     INSERT INTO p1 VALUES(5, 5);
#     INSERT INTO c1 VALUES(NULL, NULL);
#     INSERT INTO c1 VALUES(4, 4);
#     INSERT INTO c1 VALUES(5, 5);
#     SELECT count(*) FROM c1;
#   }
# } {3}
# do_test e_fkey-47.2 {
#   execsql {
#     UPDATE p1 SET b = 10 WHERE b = 5;
#     SELECT d, c FROM c1;
#   }
# } {{} {} 4 4 5 10}
# do_test e_fkey-47.3 {
#   execsql {
#     UPDATE p1 SET b = 11 WHERE b = 4;
#     SELECT d, c FROM c1;
#   }
# } {{} {} 4 11 5 10}
# do_test e_fkey-47.4 {
#   execsql { 
#     UPDATE p1 SET b = 6 WHERE b IS NULL;
#     SELECT d, c FROM c1;
#   }
# } {{} {} 4 11 5 10}
# do_test e_fkey-46.5 {
#   execsql { SELECT * FROM p1 }
# } {{} 6 4 11 5 10}

# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-65058-57158
# #
# # Test an example from the "ON DELETE and ON UPDATE Actions" section 
# # of foreignkeys.html.
# #
# drop_all_tables
# do_test e_fkey-48.1 {
#   execsql {
#     CREATE TABLE artist(
#       artistid    INTEGER PRIMARY KEY, 
#       artistname  TEXT
#     );
#     CREATE TABLE track(
#       trackid     INTEGER,
#       trackname   TEXT, 
#       trackartist INTEGER REFERENCES artist(artistid) ON UPDATE CASCADE
#     );

#     INSERT INTO artist VALUES(1, 'Dean Martin');
#     INSERT INTO artist VALUES(2, 'Frank Sinatra');
#     INSERT INTO track VALUES(11, 'That''s Amore', 1);
#     INSERT INTO track VALUES(12, 'Christmas Blues', 1);
#     INSERT INTO track VALUES(13, 'My Way', 2);
#   }
# } {}
# do_test e_fkey-48.2 {
#   execsql {
#     UPDATE artist SET artistid = 100 WHERE artistname = 'Dean Martin';
#   }
# } {}
# do_test e_fkey-48.3 {
#   execsql { SELECT * FROM artist }
# } {2 {Frank Sinatra} 100 {Dean Martin}}
# do_test e_fkey-48.4 {
#   execsql { SELECT * FROM track }
# } {11 {That's Amore} 100 12 {Christmas Blues} 100 13 {My Way} 2}


# #-------------------------------------------------------------------------
# # Verify that adding an FK action does not absolve the user of the 
# # requirement not to violate the foreign key constraint.
# #
# # EVIDENCE-OF: R-53968-51642 Configuring an ON UPDATE or ON DELETE
# # action does not mean that the foreign key constraint does not need to
# # be satisfied.
# #
# drop_all_tables
# do_test e_fkey-49.1 {
#   execsql {
#     CREATE TABLE parent(a COLLATE nocase, b, c, PRIMARY KEY(c, a));
#     CREATE TABLE child(d DEFAULT 'a', e, f DEFAULT 'c',
#       FOREIGN KEY(f, d) REFERENCES parent ON UPDATE SET DEFAULT
#     );

#     INSERT INTO parent VALUES('A', 'b', 'c');
#     INSERT INTO parent VALUES('ONE', 'two', 'three');
#     INSERT INTO child VALUES('one', 'two', 'three');
#   }
# } {}
# do_test e_fkey-49.2 {
#   execsql {
#     BEGIN;
#       UPDATE parent SET a = '' WHERE a = 'oNe';
#       SELECT * FROM child;
#   }
# } {a two c}
# do_test e_fkey-49.3 {
#   execsql {
#     ROLLBACK;
#     DELETE FROM parent WHERE a = 'A';
#     SELECT * FROM parent;
#   }
# } {ONE two three}
# do_test e_fkey-49.4 {
#   catchsql { UPDATE parent SET a = '' WHERE a = 'oNe' }
# } {1 {FOREIGN KEY constraint failed}}


# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-11856-19836
# #
# # Test an example from the "ON DELETE and ON UPDATE Actions" section 
# # of foreignkeys.html. This example shows that adding an "ON DELETE DEFAULT"
# # clause does not abrogate the need to satisfy the foreign key constraint
# # (R-28220-46694).
# #
# # EVIDENCE-OF: R-28220-46694 For example, if an "ON DELETE SET DEFAULT"
# # action is configured, but there is no row in the parent table that
# # corresponds to the default values of the child key columns, deleting a
# # parent key while dependent child keys exist still causes a foreign key
# # violation.
# #
# drop_all_tables
# do_test e_fkey-50.1 {
#   execsql {
#     CREATE TABLE artist(
#       artistid    INTEGER PRIMARY KEY, 
#       artistname  TEXT
#     );
#     CREATE TABLE track(
#       trackid     INTEGER,
#       trackname   TEXT, 
#       trackartist INTEGER DEFAULT 0 REFERENCES artist(artistid) ON DELETE SET DEFAULT
#     );
#     INSERT INTO artist VALUES(3, 'Sammy Davis Jr.');
#     INSERT INTO track VALUES(14, 'Mr. Bojangles', 3);
#   }
# } {}
# do_test e_fkey-50.2 {
#   catchsql { DELETE FROM artist WHERE artistname = 'Sammy Davis Jr.' }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-50.3 {
#   execsql {
#     INSERT INTO artist VALUES(0, 'Unknown Artist');
#     DELETE FROM artist WHERE artistname = 'Sammy Davis Jr.';
#   }
# } {}
# do_test e_fkey-50.4 {
#   execsql { SELECT * FROM artist }
# } {0 {Unknown Artist}}
# do_test e_fkey-50.5 {
#   execsql { SELECT * FROM track }
# } {14 {Mr. Bojangles} 0}

# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-09564-22170
# #
# # Check that the order of steps in an UPDATE or DELETE on a parent 
# # table is as follows:
# #
# #   1. Execute applicable BEFORE trigger programs,
# #   2. Check local (non foreign key) constraints,
# #   3. Update or delete the row in the parent table,
# #   4. Perform any required foreign key actions,
# #   5. Execute applicable AFTER trigger programs. 
# #
# drop_all_tables
# do_test e_fkey-51.1 {
#   proc maxparent {args} { db one {SELECT max(x) FROM parent} }
#   db func maxparent maxparent

#   execsql {
#     CREATE TABLE parent(x PRIMARY KEY);

#     CREATE TRIGGER bu BEFORE UPDATE ON parent BEGIN
#       INSERT INTO parent VALUES(new.x-old.x);
#     END;
#     CREATE TABLE child(
#       a DEFAULT (maxparent()) REFERENCES parent ON UPDATE SET DEFAULT
#     );
#     CREATE TRIGGER au AFTER UPDATE ON parent BEGIN
#       INSERT INTO parent VALUES(new.x+old.x);
#     END;

#     INSERT INTO parent VALUES(1);
#     INSERT INTO child VALUES(1);
#   }
# } {}
# do_test e_fkey-51.2 {
#   execsql {
#     UPDATE parent SET x = 22;
#     SELECT * FROM parent ORDER BY rowid; SELECT 'xxx' ; SELECT a FROM child;
#   }
# } {22 21 23 xxx 22}
# do_test e_fkey-51.3 {
#   execsql {
#     DELETE FROM child;
#     DELETE FROM parent;
#     INSERT INTO parent VALUES(-1);
#     INSERT INTO child VALUES(-1);
#     UPDATE parent SET x = 22;
#     SELECT * FROM parent ORDER BY rowid; SELECT 'xxx' ; SELECT a FROM child;
#   }
# } {22 23 21 xxx 23}


# #-------------------------------------------------------------------------
# # Verify that ON UPDATE actions only actually take place if the parent key
# # is set to a new value that is distinct from the old value. The default
# # collation sequence and affinity are used to determine if the new value
# # is 'distinct' from the old or not.
# #
# # EVIDENCE-OF: R-27383-10246 An ON UPDATE action is only taken if the
# # values of the parent key are modified so that the new parent key
# # values are not equal to the old.
# #
# drop_all_tables
# do_test e_fkey-52.1 {
#   execsql {
#     CREATE TABLE zeus(a INTEGER COLLATE NOCASE, b, PRIMARY KEY(a, b));
#     CREATE TABLE apollo(c, d, 
#       FOREIGN KEY(c, d) REFERENCES zeus ON UPDATE CASCADE
#     );
#     INSERT INTO zeus VALUES('abc', 'xyz');
#     INSERT INTO apollo VALUES('ABC', 'xyz');
#   }
#   execsql {
#     UPDATE zeus SET a = 'aBc';
#     SELECT * FROM apollo;
#   }
# } {ABC xyz}
# do_test e_fkey-52.2 {
#   execsql {
#     UPDATE zeus SET a = 1, b = 1;
#     SELECT * FROM apollo;
#   }
# } {1 1}
# do_test e_fkey-52.3 {
#   execsql {
#     UPDATE zeus SET a = 1, b = 1;
#     SELECT typeof(c), c, typeof(d), d FROM apollo;
#   }
# } {integer 1 integer 1}
# do_test e_fkey-52.4 {
#   execsql {
#     UPDATE zeus SET a = '1';
#     SELECT typeof(c), c, typeof(d), d FROM apollo;
#   }
# } {integer 1 integer 1}
# do_test e_fkey-52.5 {
#   execsql {
#     UPDATE zeus SET b = '1';
#     SELECT typeof(c), c, typeof(d), d FROM apollo;
#   }
# } {integer 1 text 1}
# do_test e_fkey-52.6 {
#   execsql {
#     UPDATE zeus SET b = NULL;
#     SELECT typeof(c), c, typeof(d), d FROM apollo;
#   }
# } {integer 1 null {}}

# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-35129-58141
# #
# # Test an example from the "ON DELETE and ON UPDATE Actions" section 
# # of foreignkeys.html. This example demonstrates that ON UPDATE actions
# # only take place if at least one parent key column is set to a value 
# # that is distinct from its previous value.
# #
# drop_all_tables
# do_test e_fkey-53.1 {
#   execsql {
#     CREATE TABLE parent(x PRIMARY KEY);
#     CREATE TABLE child(y REFERENCES parent ON UPDATE SET NULL);
#     INSERT INTO parent VALUES('key');
#     INSERT INTO child VALUES('key');
#   }
# } {}
# do_test e_fkey-53.2 {
#   execsql {
#     UPDATE parent SET x = 'key';
#     SELECT IFNULL(y, 'null') FROM child;
#   }
# } {key}
# do_test e_fkey-53.3 {
#   execsql {
#     UPDATE parent SET x = 'key2';
#     SELECT IFNULL(y, 'null') FROM child;
#   }
# } {null}

# ###########################################################################
# ### SECTION 5: CREATE, ALTER and DROP TABLE commands
# ###########################################################################

# #-------------------------------------------------------------------------
# # Test that parent keys are not checked when tables are created.
# #
# # EVIDENCE-OF: R-36018-21755 The parent key definitions of foreign key
# # constraints are not checked when a table is created.
# #
# # EVIDENCE-OF: R-25384-39337 There is nothing stopping the user from
# # creating a foreign key definition that refers to a parent table that
# # does not exist, or to parent key columns that do not exist or are not
# # collectively bound by a PRIMARY KEY or UNIQUE constraint.
# #
# # Child keys are checked to ensure all component columns exist. If parent
# # key columns are explicitly specified, SQLite checks to make sure there
# # are the same number of columns in the child and parent keys. (TODO: This
# # is tested but does not correspond to any testable statement.)
# #
# # Also test that the above statements are true regardless of whether or not
# # foreign keys are enabled:  "A CREATE TABLE command operates the same whether
# # or not foreign key constraints are enabled."
# #
# # EVIDENCE-OF: R-08908-23439 A CREATE TABLE command operates the same
# # whether or not foreign key constraints are enabled.
# # 
# foreach {tn zCreateTbl lRes} {
#   1 "CREATE TABLE t1(a, b REFERENCES t1)"                            {0 {}}
#   2 "CREATE TABLE t1(a, b REFERENCES t2)"                            {0 {}}
#   3 "CREATE TABLE t1(a, b, FOREIGN KEY(a,b) REFERENCES t1)"          {0 {}}
#   4 "CREATE TABLE t1(a, b, FOREIGN KEY(a,b) REFERENCES t2)"          {0 {}}
#   5 "CREATE TABLE t1(a, b, FOREIGN KEY(a,b) REFERENCES t2)"          {0 {}}
#   6 "CREATE TABLE t1(a, b, FOREIGN KEY(a,b) REFERENCES t2(n,d))"     {0 {}}
#   7 "CREATE TABLE t1(a, b, FOREIGN KEY(a,b) REFERENCES t1(a,b))"     {0 {}}

#   A "CREATE TABLE t1(a, b, FOREIGN KEY(c,b) REFERENCES t2)"          
#      {1 {unknown column "c" in foreign key definition}}
#   B "CREATE TABLE t1(a, b, FOREIGN KEY(c,b) REFERENCES t2(d))"          
#      {1 {number of columns in foreign key does not match the number of columns in the referenced table}}
# } {
#   do_test e_fkey-54.$tn.off {
#     drop_all_tables
#     execsql {PRAGMA foreign_keys = OFF}
#     catchsql $zCreateTbl
#   } $lRes
#   do_test e_fkey-54.$tn.on {
#     drop_all_tables
#     execsql {PRAGMA foreign_keys = ON}
#     catchsql $zCreateTbl
#   } $lRes
# }

# #-------------------------------------------------------------------------
# # EVIDENCE-OF: R-47952-62498 It is not possible to use the "ALTER TABLE
# # ... ADD COLUMN" syntax to add a column that includes a REFERENCES
# # clause, unless the default value of the new column is NULL. Attempting
# # to do so returns an error.
# #
# proc test_efkey_6 {tn zAlter isError} {
#   drop_all_tables 

#   do_test e_fkey-56.$tn.1 "
#     execsql { CREATE TABLE tbl(a, b) }
#     [list catchsql $zAlter]
#   " [lindex {{0 {}} {1 {Cannot add a REFERENCES column with non-NULL default value}}} $isError]

# }

# test_efkey_6 1 "ALTER TABLE tbl ADD COLUMN c REFERENCES xx" 0
# test_efkey_6 2 "ALTER TABLE tbl ADD COLUMN c DEFAULT NULL REFERENCES xx" 0
# test_efkey_6 3 "ALTER TABLE tbl ADD COLUMN c DEFAULT 0 REFERENCES xx" 1

# #-------------------------------------------------------------------------
# # Test that ALTER TABLE adjusts REFERENCES clauses when the parent table
# # is RENAMED.
# #
# # EVIDENCE-OF: R-47080-02069 If an "ALTER TABLE ... RENAME TO" command
# # is used to rename a table that is the parent table of one or more
# # foreign key constraints, the definitions of the foreign key
# # constraints are modified to refer to the parent table by its new name
# #
# # Test that these adjustments are visible in the sqlite_master table.
# #
# # EVIDENCE-OF: R-63827-54774 The text of the child CREATE TABLE
# # statement or statements stored in the sqlite_master table are modified
# # to reflect the new parent table name.
# #
# do_test e_fkey-56.1 {
#   drop_all_tables
#   execsql {
#     CREATE TABLE 'p 1 "parent one"'(a REFERENCES 'p 1 "parent one"', b, PRIMARY KEY(b));

#     CREATE TABLE c1(c, d REFERENCES 'p 1 "parent one"' ON UPDATE CASCADE);
#     CREATE TABLE c2(e, f, FOREIGN KEY(f) REFERENCES 'p 1 "parent one"' ON UPDATE CASCADE);
#     CREATE TABLE c3(e, 'f col 2', FOREIGN KEY('f col 2') REFERENCES 'p 1 "parent one"' ON UPDATE CASCADE);

#     INSERT INTO 'p 1 "parent one"' VALUES(1, 1);
#     INSERT INTO c1 VALUES(1, 1);
#     INSERT INTO c2 VALUES(1, 1);
#     INSERT INTO c3 VALUES(1, 1);

#     -- CREATE TABLE q(a, b, PRIMARY KEY(b));
#   }
# } {}
# do_test e_fkey-56.2 {
#   execsql { ALTER TABLE 'p 1 "parent one"' RENAME TO p }
# } {}
# do_test e_fkey-56.3 {
#   execsql {
#     UPDATE p SET a = 'xxx', b = 'xxx';
#     SELECT * FROM p;
#     SELECT * FROM c1;
#     SELECT * FROM c2;
#     SELECT * FROM c3;
#   }
# } {xxx xxx 1 xxx 1 xxx 1 xxx}
# do_test e_fkey-56.4 {
#   execsql { SELECT sql FROM sqlite_master WHERE type = 'table'}
# } [list                                                                     \
#   {CREATE TABLE "p"(a REFERENCES "p", b, PRIMARY KEY(b))}                   \
#   {CREATE TABLE c1(c, d REFERENCES "p" ON UPDATE CASCADE)}                  \
#   {CREATE TABLE c2(e, f, FOREIGN KEY(f) REFERENCES "p" ON UPDATE CASCADE)}  \
#   {CREATE TABLE c3(e, 'f col 2', FOREIGN KEY('f col 2') REFERENCES "p" ON UPDATE CASCADE)} \
# ]

# #-------------------------------------------------------------------------
# # Check that a DROP TABLE does an implicit DELETE FROM. Which does not
# # cause any triggers to fire, but does fire foreign key actions.
# #
# # EVIDENCE-OF: R-14208-23986 If foreign key constraints are enabled when
# # it is prepared, the DROP TABLE command performs an implicit DELETE to
# # remove all rows from the table before dropping it.
# #
# # EVIDENCE-OF: R-11078-03945 The implicit DELETE does not cause any SQL
# # triggers to fire, but may invoke foreign key actions or constraint
# # violations.
# #
# do_test e_fkey-57.1 {
#   drop_all_tables
#   execsql {
#     CREATE TABLE p(a, b, PRIMARY KEY(a, b));

#     CREATE TABLE c1(c, d, FOREIGN KEY(c, d) REFERENCES p ON DELETE SET NULL);
#     CREATE TABLE c2(c, d, FOREIGN KEY(c, d) REFERENCES p ON DELETE SET DEFAULT);
#     CREATE TABLE c3(c, d, FOREIGN KEY(c, d) REFERENCES p ON DELETE CASCADE);
#     CREATE TABLE c4(c, d, FOREIGN KEY(c, d) REFERENCES p ON DELETE RESTRICT);
#     CREATE TABLE c5(c, d, FOREIGN KEY(c, d) REFERENCES p ON DELETE NO ACTION);

#     CREATE TABLE c6(c, d, 
#       FOREIGN KEY(c, d) REFERENCES p ON DELETE RESTRICT 
#       DEFERRABLE INITIALLY DEFERRED
#     );
#     CREATE TABLE c7(c, d, 
#       FOREIGN KEY(c, d) REFERENCES p ON DELETE NO ACTION
#       DEFERRABLE INITIALLY DEFERRED
#     );

#     CREATE TABLE log(msg);
#     CREATE TRIGGER tt AFTER DELETE ON p BEGIN
#       INSERT INTO log VALUES('delete ' || old.rowid);
#     END;
#   }
# } {}

# do_test e_fkey-57.2 {
#   execsql {
#     INSERT INTO p VALUES('a', 'b');
#     INSERT INTO c1 VALUES('a', 'b');
#     INSERT INTO c2 VALUES('a', 'b');
#     INSERT INTO c3 VALUES('a', 'b');
#     BEGIN;
#       DROP TABLE p;
#       SELECT * FROM c1;
#   }
# } {{} {}}
# do_test e_fkey-57.3 {
#   execsql { SELECT * FROM c2 }
# } {{} {}}
# do_test e_fkey-57.4 {
#   execsql { SELECT * FROM c3 }
# } {}
# do_test e_fkey-57.5 {
#   execsql { SELECT * FROM log }
# } {}
# do_test e_fkey-57.6 {
#   execsql ROLLBACK
# } {}
# do_test e_fkey-57.7 {
#   execsql {
#     BEGIN;
#       DELETE FROM p;
#       SELECT * FROM log;
#     ROLLBACK;
#   }
# } {{delete 1}}

# #-------------------------------------------------------------------------
# # If an IMMEDIATE foreign key fails as a result of a DROP TABLE, the
# # DROP TABLE command fails.
# #
# # EVIDENCE-OF: R-32768-47925 If an immediate foreign key constraint is
# # violated, the DROP TABLE statement fails and the table is not dropped.
# #
# do_test e_fkey-58.1 {
#   execsql { 
#     DELETE FROM c1;
#     DELETE FROM c2;
#     DELETE FROM c3;
#   }
#   execsql { INSERT INTO c5 VALUES('a', 'b') }
#   catchsql { DROP TABLE p }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-58.2 {
#   execsql { SELECT * FROM p }
# } {a b}
# do_test e_fkey-58.3 {
#   catchsql {
#     BEGIN;
#       DROP TABLE p;
#   }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-58.4 {
#   execsql {
#     SELECT * FROM p;
#     SELECT * FROM c5;
#     ROLLBACK;
#   }
# } {a b a b}

# #-------------------------------------------------------------------------
# # If a DEFERRED foreign key fails as a result of a DROP TABLE, attempting
# # to commit the transaction fails unless the violation is fixed.
# #
# # EVIDENCE-OF: R-05903-08460 If a deferred foreign key constraint is
# # violated, then an error is reported when the user attempts to commit
# # the transaction if the foreign key constraint violations still exist
# # at that point.
# #
# do_test e_fkey-59.1 {
#   execsql { 
#     DELETE FROM c1 ; DELETE FROM c2 ; DELETE FROM c3 ;
#     DELETE FROM c4 ; DELETE FROM c5 ; DELETE FROM c6 ;
#     DELETE FROM c7 
#   }
# } {}
# do_test e_fkey-59.2 {
#   execsql { INSERT INTO c7 VALUES('a', 'b') }
#   execsql {
#     BEGIN;
#       DROP TABLE p;
#   }
# } {}
# do_test e_fkey-59.3 {
#   catchsql COMMIT
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-59.4 {
#   execsql { CREATE TABLE p(a, b, PRIMARY KEY(a, b)) }
#   catchsql COMMIT
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-59.5 {
#   execsql { INSERT INTO p VALUES('a', 'b') }
#   execsql COMMIT
# } {}

# #-------------------------------------------------------------------------
# # Any "foreign key mismatch" errors encountered while running an implicit
# # "DELETE FROM tbl" are ignored.
# #
# # EVIDENCE-OF: R-57242-37005 Any "foreign key mismatch" errors
# # encountered as part of an implicit DELETE are ignored.
# #
# drop_all_tables
# do_test e_fkey-60.1 {
#   execsql {
#     PRAGMA foreign_keys = OFF;

#     CREATE TABLE p(a PRIMARY KEY, b REFERENCES nosuchtable);
#     CREATE TABLE c1(c, d, FOREIGN KEY(c, d) REFERENCES a);
#     CREATE TABLE c2(c REFERENCES p(b), d);
#     CREATE TABLE c3(c REFERENCES p ON DELETE SET NULL, d);

#     INSERT INTO p VALUES(1, 2);
#     INSERT INTO c1 VALUES(1, 2);
#     INSERT INTO c2 VALUES(1, 2);
#     INSERT INTO c3 VALUES(1, 2);
#   }
# } {}
# do_test e_fkey-60.2 {
#   execsql { PRAGMA foreign_keys = ON }
#   catchsql { DELETE FROM p }
# } {1 {no such table: main.nosuchtable}}
# do_test e_fkey-60.3 {
#   execsql {
#     BEGIN;
#       DROP TABLE p;
#       SELECT * FROM c3;
#     ROLLBACK;
#   }
# } {{} 2}
# do_test e_fkey-60.4 {
#   execsql { CREATE TABLE nosuchtable(x PRIMARY KEY) }
#   catchsql { DELETE FROM p }
# } {1 {foreign key mismatch - "c2" referencing "p"}}
# do_test e_fkey-60.5 {
#   execsql { DROP TABLE c1 }
#   catchsql { DELETE FROM p }
# } {1 {foreign key mismatch - "c2" referencing "p"}}
# do_test e_fkey-60.6 {
#   execsql { DROP TABLE c2 }
#   execsql { DELETE FROM p }
# } {}

# #-------------------------------------------------------------------------
# # Test that the special behaviors of ALTER and DROP TABLE are only
# # activated when foreign keys are enabled. Special behaviors are:
# #
# #   1. ADD COLUMN not allowing a REFERENCES clause with a non-NULL 
# #      default value.
# #   2. Modifying foreign key definitions when a parent table is RENAMEd.
# #   3. Running an implicit DELETE FROM command as part of DROP TABLE.
# #
# # EVIDENCE-OF: R-54142-41346 The properties of the DROP TABLE and ALTER
# # TABLE commands described above only apply if foreign keys are enabled.
# #
# do_test e_fkey-61.1.1 {
#   drop_all_tables
#   execsql { CREATE TABLE t1(a, b) }
#   catchsql { ALTER TABLE t1 ADD COLUMN c DEFAULT 'xxx' REFERENCES t2 }
# } {1 {Cannot add a REFERENCES column with non-NULL default value}}
# do_test e_fkey-61.1.2 {
#   execsql { PRAGMA foreign_keys = OFF }
#   execsql { ALTER TABLE t1 ADD COLUMN c DEFAULT 'xxx' REFERENCES t2 }
#   execsql { SELECT sql FROM sqlite_master WHERE name = 't1' }
# } {{CREATE TABLE t1(a, b, c DEFAULT 'xxx' REFERENCES t2)}}
# do_test e_fkey-61.1.3 {
#   execsql { PRAGMA foreign_keys = ON }
# } {}

# do_test e_fkey-61.2.1 {
#   drop_all_tables
#   execsql {
#     CREATE TABLE p(a UNIQUE);
#     CREATE TABLE c(b REFERENCES p(a));
#     BEGIN;
#       ALTER TABLE p RENAME TO parent;
#       SELECT sql FROM sqlite_master WHERE name = 'c';
#     ROLLBACK;
#   }
# } {{CREATE TABLE c(b REFERENCES "parent"(a))}}
# do_test e_fkey-61.2.2 {
#   execsql {
#     PRAGMA foreign_keys = OFF;
#     ALTER TABLE p RENAME TO parent;
#     SELECT sql FROM sqlite_master WHERE name = 'c';
#   }
# } {{CREATE TABLE c(b REFERENCES p(a))}}
# do_test e_fkey-61.2.3 {
#   execsql { PRAGMA foreign_keys = ON }
# } {}

# do_test e_fkey-61.3.1 {
#   drop_all_tables
#   execsql {
#     CREATE TABLE p(a UNIQUE);
#     CREATE TABLE c(b REFERENCES p(a) ON DELETE SET NULL);
#     INSERT INTO p VALUES('x');
#     INSERT INTO c VALUES('x');
#     BEGIN;
#       DROP TABLE p;
#       SELECT * FROM c;
#     ROLLBACK;
#   }
# } {{}}
# do_test e_fkey-61.3.2 {
#   execsql {
#     PRAGMA foreign_keys = OFF;
#     DROP TABLE p;
#     SELECT * FROM c;
#   }
# } {x}
# do_test e_fkey-61.3.3 {
#   execsql { PRAGMA foreign_keys = ON }
# } {}

# ###########################################################################
# ### SECTION 6: Limits and Unsupported Features
# ###########################################################################

# #-------------------------------------------------------------------------
# # Test that MATCH clauses are parsed, but SQLite treats every foreign key
# # constraint as if it were "MATCH SIMPLE".
# #
# # EVIDENCE-OF: R-24728-13230 SQLite parses MATCH clauses (i.e. does not
# # report a syntax error if you specify one), but does not enforce them.
# #
# # EVIDENCE-OF: R-24450-46174 All foreign key constraints in SQLite are
# # handled as if MATCH SIMPLE were specified.
# #
# foreach zMatch [list SIMPLE PARTIAL FULL Simple parTIAL FuLL ] {
#   drop_all_tables
#   do_test e_fkey-62.$zMatch.1 {
#     execsql "
#       CREATE TABLE p(a, b, c, PRIMARY KEY(b, c));
#       CREATE TABLE c(d, e, f, FOREIGN KEY(e, f) REFERENCES p MATCH $zMatch);
#     "
#   } {}
#   do_test e_fkey-62.$zMatch.2 {
#     execsql { INSERT INTO p VALUES(1, 2, 3)         }

#     # MATCH SIMPLE behavior: Allow any child key that contains one or more
#     # NULL value to be inserted. Non-NULL values do not have to map to any
#     # parent key values, so long as at least one field of the child key is
#     # NULL.
#     execsql { INSERT INTO c VALUES('w', 2, 3)       }
#     execsql { INSERT INTO c VALUES('x', 'x', NULL)  }
#     execsql { INSERT INTO c VALUES('y', NULL, 'x')  }
#     execsql { INSERT INTO c VALUES('z', NULL, NULL) }

#     # Check that the FK is enforced properly if there are no NULL values 
#     # in the child key columns.
#     catchsql { INSERT INTO c VALUES('a', 2, 4) }
#   } {1 {FOREIGN KEY constraint failed}}
# }

# #-------------------------------------------------------------------------
# # Test that SQLite does not support the SET CONSTRAINT statement. And
# # that it is possible to create both immediate and deferred constraints.
# #
# # EVIDENCE-OF: R-21599-16038 In SQLite, a foreign key constraint is
# # permanently marked as deferred or immediate when it is created.
# #
# drop_all_tables
# do_test e_fkey-62.1 {
#   catchsql { SET CONSTRAINTS ALL IMMEDIATE }
# } {1 {near "SET": syntax error}}
# do_test e_fkey-62.2 {
#   catchsql { SET CONSTRAINTS ALL DEFERRED }
# } {1 {near "SET": syntax error}}

# do_test e_fkey-62.3 {
#   execsql {
#     CREATE TABLE p(a, b, PRIMARY KEY(a, b));
#     CREATE TABLE cd(c, d, 
#       FOREIGN KEY(c, d) REFERENCES p DEFERRABLE INITIALLY DEFERRED);
#     CREATE TABLE ci(c, d, 
#       FOREIGN KEY(c, d) REFERENCES p DEFERRABLE INITIALLY IMMEDIATE);
#     BEGIN;
#   }
# } {}
# do_test e_fkey-62.4 {
#   catchsql { INSERT INTO ci VALUES('x', 'y') }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-62.5 {
#   catchsql { INSERT INTO cd VALUES('x', 'y') }
# } {0 {}}
# do_test e_fkey-62.6 {
#   catchsql { COMMIT }
# } {1 {FOREIGN KEY constraint failed}}
# do_test e_fkey-62.7 {
#   execsql { 
#     DELETE FROM cd;
#     COMMIT;
#   }
# } {}

# #-------------------------------------------------------------------------
# # Test that the maximum recursion depth of foreign key action programs is
# # governed by the SQLITE_MAX_TRIGGER_DEPTH and SQLITE_LIMIT_TRIGGER_DEPTH
# # settings.
# #
# # EVIDENCE-OF: R-42264-30503 The SQLITE_MAX_TRIGGER_DEPTH and
# # SQLITE_LIMIT_TRIGGER_DEPTH settings determine the maximum allowable
# # depth of trigger program recursion. For the purposes of these limits,
# # foreign key actions are considered trigger programs.
# #
# proc test_on_delete_recursion {limit} {
#   drop_all_tables
#   execsql { 
#     BEGIN;
#     CREATE TABLE t0(a PRIMARY KEY, b);
#     INSERT INTO t0 VALUES('x0', NULL);
#   }
#   for {set i 1} {$i <= $limit} {incr i} {
#     execsql "
#       CREATE TABLE t$i (
#         a PRIMARY KEY, b REFERENCES t[expr $i-1] ON DELETE CASCADE
#       );
#       INSERT INTO t$i VALUES('x$i', 'x[expr $i-1]');
#     "
#   }
#   execsql COMMIT
#   catchsql "
#     DELETE FROM t0;
#     SELECT count(*) FROM t$limit;
#   "
# }
# proc test_on_update_recursion {limit} {
#   drop_all_tables
#   execsql { 
#     BEGIN;
#     CREATE TABLE t0(a PRIMARY KEY);
#     INSERT INTO t0 VALUES('xxx');
#   }
#   for {set i 1} {$i <= $limit} {incr i} {
#     set j [expr $i-1]

#     execsql "
#       CREATE TABLE t$i (a PRIMARY KEY REFERENCES t$j ON UPDATE CASCADE);
#       INSERT INTO t$i VALUES('xxx');
#     "
#   }
#   execsql COMMIT
#   catchsql "
#     UPDATE t0 SET a = 'yyy';
#     SELECT NOT (a='yyy') FROM t$limit;
#   "
# }

# # If the current build was created using clang with the -fsanitize=address
# # switch, then the library uses considerably more stack space than usual.
# # So much more, that some of the following tests cause stack overflows
# # if they are run under this configuration.
# #
# if {[clang_sanitize_address]==0} {
#   do_test e_fkey-63.1.1 {
#     test_on_delete_recursion $SQLITE_MAX_TRIGGER_DEPTH
#   } {0 0}
#   do_test e_fkey-63.1.2 {
#     test_on_delete_recursion [expr $SQLITE_MAX_TRIGGER_DEPTH+1]
#   } {1 {too many levels of trigger recursion}}
#   do_test e_fkey-63.1.3 {
#     sqlite3_limit db SQLITE_LIMIT_TRIGGER_DEPTH 5
#       test_on_delete_recursion 5
#   } {0 0}
#   do_test e_fkey-63.1.4 {
#     test_on_delete_recursion 6
#   } {1 {too many levels of trigger recursion}}
#   do_test e_fkey-63.1.5 {
#     sqlite3_limit db SQLITE_LIMIT_TRIGGER_DEPTH 1000000
#   } {5}
#   do_test e_fkey-63.2.1 {
#     test_on_update_recursion $SQLITE_MAX_TRIGGER_DEPTH
#   } {0 0}
#   do_test e_fkey-63.2.2 {
#     test_on_update_recursion [expr $SQLITE_MAX_TRIGGER_DEPTH+1]
#   } {1 {too many levels of trigger recursion}}
#   do_test e_fkey-63.2.3 {
#     sqlite3_limit db SQLITE_LIMIT_TRIGGER_DEPTH 5
#       test_on_update_recursion 5
#   } {0 0}
#   do_test e_fkey-63.2.4 {
#     test_on_update_recursion 6
#   } {1 {too many levels of trigger recursion}}
#   do_test e_fkey-63.2.5 {
#     sqlite3_limit db SQLITE_LIMIT_TRIGGER_DEPTH 1000000
#   } {5}
# }

# #-------------------------------------------------------------------------
# # The setting of the recursive_triggers pragma does not affect foreign
# # key actions.
# #
# # EVIDENCE-OF: R-44355-00270 The PRAGMA recursive_triggers setting does
# # not affect the operation of foreign key actions.
# #
# foreach recursive_triggers_setting [list 0 1 ON OFF] {
#   drop_all_tables
#   execsql "PRAGMA recursive_triggers = $recursive_triggers_setting"

#   do_test e_fkey-64.$recursive_triggers_setting.1 {
#     execsql {
#       CREATE TABLE t1(a PRIMARY KEY, b REFERENCES t1 ON DELETE CASCADE);
#       INSERT INTO t1 VALUES(1, NULL);
#       INSERT INTO t1 VALUES(2, 1);
#       INSERT INTO t1 VALUES(3, 2);
#       INSERT INTO t1 VALUES(4, 3);
#       INSERT INTO t1 VALUES(5, 4);
#       SELECT count(*) FROM t1;
#     }
#   } {5}
#   do_test e_fkey-64.$recursive_triggers_setting.2 {
#     execsql { SELECT count(*) FROM t1 WHERE a = 1 }
#   } {1}
#   do_test e_fkey-64.$recursive_triggers_setting.3 {
#     execsql { 
#       DELETE FROM t1 WHERE a = 1;
#       SELECT count(*) FROM t1;
#     }
#   } {0}
# }

finish_test