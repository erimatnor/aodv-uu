#!/bin/bash
#
#----------------------------------------------------------------------------- 
# Copyright (C) 2001 Uppsala University and Ericsson Telecom AB.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
# Authors: Erik Nordström <erik.nordstrom@it.uu.se>
#          Björn Wiberg <bjorn.wiberg@home.se>
#-----------------------------------------------------------------------------

# This script will install symlinks into a given ns-2 source tree,
# pointing to the aodv-uu modified versions of the corresponding
# files. Running it a second time will revert the operation.
#
# Usage: ./install-links.sh /path/to/ns-2.xx

currdir=`pwd`

usage="$0 /path/to/ns-2.xx/"

shift $(($OPTIND - 1))

if [ -z "$@" ]; then
    echo $usage
    exit 1
fi

ns_path=$1
ns_name=`basename ${ns_path%/}`
local_path=$ns_name

if ! echo $ns_name | grep ns &>/dev/null; then
    echo "It does not appear that the path you suggested is a path to a NS source tree"
    exit 1
fi

if [ ! -d $local_path ]; then
    echo
    echo "No local directory \"$local_path\" found that matches the ns-2 version in the given path."
    exit;
fi
echo "Installing files:"
echo
for file in `find ./$local_path -type f -regex '.*\.cc\|.*\.h\|.*.tcl\|.*\.in' -print `; do
    file=${file#./}
    filepath=`echo $file | sed -e "s/$local_path\///"`
    filename=`basename $file`
    
    #echo "$ns_path/$filepath -> $currdir/$file"
    pushd `dirname $ns_path/$filepath` &> /dev/null
    echo "Entering directory $ns_path/$filepath"
    echo $filename
    if [ -f $filename.orig ]; then
	echo "Found old backup file: $filename.orig"
	echo "Reverting previous install..."
	rm -f $filename
	mv $filename.orig $filename
    else
	mv $filename $filename.orig
	ln -s $currdir/$file .
    fi
    popd &> /dev/null
    #ln -s -f $currdir/$file $ns_path/$file
done
