#!/bin/bash
# ****************************************************************************
# Script to launch the tunnel client.
# ****************************************************************************
# You need to run this as root. But the client drops its privileges (-u nobody)
# as soon as it has finished its privileged actions.
# ****************************************************************************
# -m 7 says to start with 7 worker threads. This may help to avoid hangs, and
# overcome latency. Your Mileage May Vary. I tested it without this parameter.
# If it does hang, it should sort itself out after a minute or so, or you can
# simply kill it (kill -USR1 or kill -9) and start again. It won't hang if you
# keep it busy.
# *****************************************************************************
# Procedure
# VVVVVVVVV
# Before you start, check that you can access
#           https://e2systems.dynu.com/about
#****************************************************************************
# If you have to specify a proxy for your browser, you need to add
#
# -l local.web.proxy:port
#
# to the tundrive command line
#
# where proxy is the address of the Web proxy on your and port is the port it
# listens on, to the tundrive command line below, to send the requests via the
# proxy.
#
# The existence of a proxy would affect the routing reconfiguration; see below.
# ***************************************************************************
# This script won't work if:
# -   Your local network is already 192.168.4.0/24!
# -   You already have a network device tun1
#
# You can change tun1 to anything you like, but I would need to restart the
# server with a corresponding address if the address had to be changed.
# **************************************************************************
# Make sure the tunnel device is available
modprobe tun
lsmod | grep tun
# Create the tunnel - The first line is for camping-abers.
#./tundrive -u nobody -m 3 -l 192.168.20.1:3128 tun1 192.168.4.2 http://e2systems.homelinux.com:80 &
#./tundrive -d 4 -v 4 -u nobody -m 2 tun2 192.168.0.12:443 &
# Test through e2systems.dynu.com
./tundrive -v 4 -e tunsettings.sh -u nobody -m 2 -l 10.200.34.75:8080 tun2 https://82.40.220.92:443 &
# Local network test
#./tundrive -d 4 -u nobody -m 2 tun1 192.168.0.109:8070 &
# Wait for the tunnel to appear
until ifconfig tun2
do
    sleep 1
done
set -- `ifconfig tun2 | gawk 'BEGIN { getline
getline
split($2, arr, ":")
split(arr[2], arr1, ".")
print arr[2] " " arr1[1] "." arr1[2] "." arr1[3] ".1"
exit
}'`
route add $2 gw $1
ping -c 10 $1
exit
#route add -net $2 netmask 255.255.255.0 gw $1
# Check that you can reach the other end of the tunnel
# *************************************************************************
# Now reconfigure the routes
echo Beforehand ...
netstat -n -r
echo ==============
# Collect information needed for the route reconfiguration
set -- `netstat -n -r | awk '$1 == "0.0.0.0" { print $2 " " $NF}'`
defgw=$1
loif=$2
loip=`ifconfig $loif | awk -F: '/inet addr:/ {nf = split($2, arr, " "); print arr[1]}'`
set -- `netstat -n -r | awk '$2 == "0.0.0.0" { print $1 " " $3}'`
lonet=$1
lonmask=$2
if [ -z "$lonet" -o -z "$defgw" -o -z "$lonmask" -o -z "$loip" ]
then
    echo Local Network $lonet
    echo Local Default Gateway $defgw
    echo Local Netmask $lonmask
    echo Local IP $loip
    echo Must all be non-zero length
    exit 1
fi
# Make sure that there is a route to the local network for local traffic
route add -net $lonet netmask $lonmask gw $loip
# Make sure that there is a route to the tunnel end point through the local
# default gateway. IF YOU HAVE A PROXY, THIS NEEDS TO BE THE PROXY rather
# than the real tunnel end point
# route add e2systems.homelinux.com gw $defgw
#route add 192.168.20.1 gw $defgw
# **************************************************************************
# Remove the existing default route.
route del -net 0.0.0.0 netmask 0.0.0.0
# Create a new default route via the http tunnel.
route add -net 0.0.0.0 netmask 0.0.0.0  gw 192.168.4.2
# You would put things back by executing the del again, and re-executing
# the add with the local default gateway in place of 192.168.4.2, the local
# tunnel address.
# **************************************************************************
# Check that everything is as expected.
echo Afterwards
netstat -n -r
echo ==========
ping 192.168.4.1 &
