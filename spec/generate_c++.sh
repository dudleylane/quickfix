#!/bin/bash
set -e

# NB: FixValues.h is NOT generated here -- Generator.rb (GeneratorCPP.rb) writes
# it afterwards and would overwrite anything xsltproc produced.  The FIXT11
# Values.xsl pass is dropped too: it produced FixTValues.h, which is untracked
# and included by nothing.
xsltproc -o ../src/C++/fix40/MessageCracker.h MessageCracker.xsl FIX40.xml
xsltproc -o ../src/C++/fix41/MessageCracker.h MessageCracker.xsl FIX41.xml
xsltproc -o ../src/C++/fix42/MessageCracker.h MessageCracker.xsl FIX42.xml
xsltproc -o ../src/C++/fix43/MessageCracker.h MessageCracker.xsl FIX43.xml
xsltproc -o ../src/C++/fix44/MessageCracker.h MessageCracker.xsl FIX44.xml
xsltproc -o ../src/C++/fix50/MessageCracker.h MessageCracker.xsl FIX50.xml
xsltproc -o ../src/C++/fix50sp1/MessageCracker.h MessageCracker.xsl FIX50SP1.xml
xsltproc -o ../src/C++/fix50sp2/MessageCracker.h MessageCracker.xsl FIX50SP2.xml
xsltproc -o ../src/C++/fixt11/MessageCracker.h MessageCracker.xsl FIXT11.xml

