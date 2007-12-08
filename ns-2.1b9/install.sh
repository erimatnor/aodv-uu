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
usage="$0 path_to_unmodified_ns_tree..."

shift $(($OPTIND - 1))

if [ -z "$@" ]; then
    echo $usage
    exit 1
fi

ns_path=$1
ns_name=`basename ${ns_path%/}`

if ! echo $ns_name | grep ns &>/dev/null; then
    echo "It does not appear that the path you suggested is a path to a NS source tree"
    exit 1
fi

echo "Installing files:"
echo
for file in `find ./ -type f -print | grep -v "[CVS|~]" | grep -v .sh$`; do
    file=${file#./}
    echo "$file ---> $ns_path/$file"
    cp -a $file $ns_path/$file
done
