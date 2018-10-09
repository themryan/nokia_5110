#!/bin/bash

MODULE_NAME="Beaglebone Nokia 5110 Driver"
error_log=inst_errors_"$(date -d "today" +"%Y%m%d%H%M%S").log"

echo "$MODULE_NAME Error Log" > $error_log

linux_sources="/lib/modules/$(uname -r)/build/"

echo -e "\033[33mInstalling $MODULE_NAME\033[0m"

# Check for linux sources
if [ -d "$linux_sources" ]; then
	echo -e "\033[32mLinux headers found!!\033[0m"
else
	echo -e "\033[31mCould not find Linux headers at $linux_sources\033[0m"
	echo -e "\033[31mPlease install linux headers and try again.\033[0m"
	exit 1
fi

echo -e "\033[33mCompiling...\033[0m"
make 2> $error_log

if [ $? -eq 1 ]; then
	echo -e "\033[31mFailed to compile module nokia_5110\033[0m"
	exit 1
fi

if [ -e "/etc/udev/rules.d/99-nokiacdev.rules" ]; then
	echo -e "\033[33mUDEV rule already exists\033[0m"
else
	sudo cp 99-nokiacdev.rules /etc/udev/rules.d/ 2> $error_log

	if [ $? -eq 1 ]; then
		echo -e "\033[31mFailed to copy udev rule.\033[0m"
	fi
fi

sudo rmmod nokia_5110 > /dev/null

sudo insmod nokia_5110.ko 2> $error_log

if [ $? -eq 0 ]; then
	printf "\033[32mSuccesfully installed $MODULE_NAME\n\033[0m"
else
	echo -e "\033[31mFailed to install module $MODULE_NAME\033[0m"
	echo -e "\033[31mSee $error_log for more details\033[0m"
fi
