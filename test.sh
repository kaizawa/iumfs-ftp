#!/bin/sh
#
# Test script of iumfs filesystem
# After build iumfs, run this script as root user.
#

PATH=/usr/bin:/usr/sbin:/usr/ccs/bin:/usr/local/bin:.

daemonpid=""
# Change mount point for this test, if it exists
mnt="/var/tmp/iumfsmnt"
base="/var/tmp/iumfsbase"
# Change uid if you want to run make command as non-root user
uid="root"

init (){

	if [ "$USER" != "root" ]; then
		echo "must be run as root user!"
		fini 1	
	fi

        # Create mount point and base directory 
	for dir in ${base} ${mnt}
	do
		if [ ! -d "${dir}" ]; then
			mkdir ${dir}
			if [ "$?" -ne 0 ]; then
				echo "cannot create ${dir}"
				fini 1
			fi
		fi
	done

	# Create test directory and test text file under base dir
	mkdir ${base}/testdir
	echo "testtext" > ${base}/testdir/testfile

	sudo -u ${uid} ./configure
	make uninstall
	sudo -u ${uid} make clean
	sudo -u ${uid} make
	make install

 	# kill iumfsd fstestd
	kill_daemon

	# Just in case, umount ${mnt}
	while : 
	do
		mountexit=`mount |grep "${mnt} "`
		if [ -z "$mountexit" ]; then
			break
		fi
		umount ${mnt} > /dev/null 2>&1
	done
}

exec_mount () {
 	mount -F iumfs ftp://localhost${base}/ ${mnt}	
	return $?
}

exec_umount() {
	umount ${mnt} > /dev/null 2>&1 
	return $?
}

exec_daemon() {
	./fstestd
	if [ "$?" -eq 0 ]; then
		daemonpid=$! 
		return 0		
	fi
	return 1
}

kill_daemon(){
	
	pkill -KILL iumfsd
	pkill -KILL fstestd
	daemonpid=""
	return 0
}

run_test () {
	target=${1}
	cmd="exec_${target}"
	$cmd
	if [ "$?" -eq "0" ]; then
		echo "${target} test: pass"
	else
		echo "${target} test: fail"
		fini 1	
	fi
}

exec_getattr() {
	exec_mount
	exec_daemon

	./fstest getattr
	if [ "$?" -ne "0" ]; then
	    kill_daemon
	    exec_umount
	    return 1
	fi

	kill_daemon
	exec_umount
	return 0	
}

exec_open() {
	exec_mount
	exec_daemon

	./fstest open
	if [ "$?" -ne "0" ]; then
	    kill_daemon
	    exec_umount
	    return 1
	fi

	kill_daemon
	exec_umount
	return 0	
}

exec_read() {
	exec_mount
	exec_daemon

	./fstest read
	if [ "$?" -ne "0" ]; then
	    kill_daemon
	    exec_umount
	    return 1
	fi

	kill_daemon
	exec_umount
	return 0	
}

fini() {
	kill_daemon
	exec_umount
	rm -rf ${mnt}
	rm -rf ${base}
}

init
run_test "mount"
run_test "umount"
run_test "getattr"
run_test "open"
run_test "read"
fini
