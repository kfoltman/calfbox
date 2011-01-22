aclocal --force || exit 1
libtoolize --force --automake --copy || exit 1
autoheader --force || exit 1
autoconf --force || exit 1
automake --add-missing --copy || exit 1

