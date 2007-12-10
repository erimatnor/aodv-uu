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
#-----------------------------------------------------------------------------
# This scripts will automate the process of generating patches for AODV-UU
# support againts NS-2. Simply give the path to an unmodified NS source
# tree as input to this script to create the patch.

home=$PWD
patch=
tmp_patch="/tmp/ns-patch"

usage="$0 [-p PATCH] path_to_unmodified_ns_tree..."


cleanup() {

    if [ -d $ns_tmp_name ]; then
	rm -rf $ns_tmp_name
	rm -f $tmp_patch
    fi
}

while getopts ":p:" opt; do
    case $opt in
	p) 
	patch=$OPTARG
	;;
	\?)
	echo $usage
	exit 1
	;;
    esac
done
    

shift $(($OPTIND - 1))

if [ -z "$@" ]; then
    echo $usage
    exit 1
fi

ns_path=$1
ns_name=`basename ${ns_path%/}`
ns_tmp_name="/tmp/$ns_name.aodv-uu"

if [ -z "$patch" ]; then
    patch="$ns_name-aodv-uu.patch"
fi

if ! echo $ns_name | grep ns &>/dev/null; then
    echo "It does not appear that the path you suggested is a path to a NS source tree"
    exit 1
fi

trap cleanup EXIT

# Update the tree with new files..

echo "Making AODV-UU patch for $ns_name. Please wait..."
cp -a $ns_path $ns_tmp_name

for file in `find ./ -type f -print | grep -v "[CVS|~]" | grep -v .sh$`; do
    file=${file#./}
    #echo "* $file $ns_tmp_name/$file"
    cp -a $file $ns_tmp_name/$file
done

cd $ns_path; cd ..
diff -urN $ns_name $ns_tmp_name > $tmp_patch
cd $home
cp $tmp_patch $patch


