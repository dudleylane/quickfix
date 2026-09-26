export CXX=$1
export CXXFLAGS=$2
export LIBS=$3

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

TEMP=`mktemp -d "../zombie.XXXXXXXXX"`

if [[ ! "$TEMP" || ! -d "$TEMP" ]]; then
  echo "Could not create temp dir"
  exit 1
fi

function cleanup {
  rm -rf "$TEMP"
  echo "Deleted temp working directory $TEMP"
}

trap cleanup EXIT

cp *.cpp $TEMP
cp *.h $TEMP
cp extconf.rb $TEMP
pushd $TEMP
ruby extconf.rb
popd
cp $TEMP/Makefile Makefile.ruby
rm -rf $TEMP

# mkmf writes a Makefile that names itself as a prerequisite -- "$(TARGET_SO):
# $(OBJS) Makefile", and the same for the pre-install-rb targets. Copying it to
# Makefile.ruby leaves those references dangling, so make stops with "No rule to
# make target 'Makefile', needed by 'quickfix.so'" after compiling everything.
# Retarget them at the renamed file.
sed -i -e 's/^\($(TARGET_SO): $(OBJS) \)Makefile$/\1Makefile.ruby/' \
       -e 's/^\(pre-install-rb: \)Makefile$/\1Makefile.ruby/' \
       -e 's/^\(pre-install-rb-default: \)Makefile$/\1Makefile.ruby/' \
       Makefile.ruby

# mkmf's Makefile records no dependency on libquickfix's headers, so after a
# header change make would relink quickfix.so from the existing QuickfixRuby.o,
# compiled against the old class layouts, and the module would corrupt the heap
# at load instead of failing to build. Compile from scratch every time; CI,
# starting from a clean checkout, pays this anyway.
rm -f QuickfixRuby.o quickfix.so

make -f Makefile.ruby
