#!/bin/bash
#
# Copyright (c) 2016 Mellanox Technologies. All rights reserved.
#
# This Software is licensed under one of the following licenses:
#
# 1) under the terms of the "Common Public License 1.0" a copy of which is
#    available from the Open Source Initiative, see
#    http://www.opensource.org/licenses/cpl.php.
#
# 2) under the terms of the "The BSD License" a copy of which is
#    available from the Open Source Initiative, see
#    http://www.opensource.org/licenses/bsd-license.php.
#
# 3) under the terms of the "GNU General Public License (GPL) Version 2" a
#    copy of which is available from the Open Source Initiative, see
#    http://www.opensource.org/licenses/gpl-license.php.
#
# Licensee has the right to choose one of the above licenses.
#
# Redistributions of source code must retain the above copyright
# notice and one of the license notices.
#
# Redistributions in binary form must reproduce both the above copyright
# notice, one of the license notices in the documentation
# and/or other materials provided with the distribution.
#
SCRIPTPATH=$(cd `dirname "${BASH_SOURCE[0]}"` && pwd)

MOD_SYMVERS=${SCRIPTPATH}/ib.symvers.extra
KVER=${1:-$(uname -r)}

# Create empty symvers file
echo -n "" > $MOD_SYMVERS


ib_mod=
crc_found=0
crc_mod_str="__crc_"
modules_pat="$crc_mod_str| T"
for mod in $(find /lib/modules/$KVER/updates -name "*.ko" 2>/dev/null)
do
	ib_mod=$(/sbin/modinfo -F filename -k "$KVER" $mod 2>/dev/null)
	if [ ! -e "$ib_mod" ]; then
		continue
	fi
	echo "Getting symbol versions from $ib_mod ..."
	while read -r line
	do
		if echo "$line" | grep -q "$crc_mod_str"; then
			crc_found=1
		else
			if [ "$crc_found" != 0 ]; then
				continue
			fi
		fi
		file=$(echo $line | cut -f1 -d: | sed -r -e 's@\./@@' -e 's@.ko(\S)*@@' -e "s@$PWD/@@")
		crc=$(echo $line | cut -f2 -d: | cut -f1 -d" ")
		sym=$(echo $line | cut -f2 -d: | cut -f3 -d" " | sed -e 's/__crc_//g')
		echo -e "0x$crc\t$sym\t$file\tEXPORT_SYMBOL\t" >> $MOD_SYMVERS
	done < <(nm -o $ib_mod | grep -E "$modules_pat")
done

if [ ! -e "$ib_mod" ]; then
	echo "-E- Cannot locate mlnx ofed ib modules!" >&2
	exit 1
fi

if [ ! -s "$MOD_SYMVERS" ]; then
	echo "-W- Could not get list of ib symbols." >&2
fi