#!/bin/bash
PATH=/home/e2soft/e2prox:$PATH
export PATH
tundrive -c 1:server.pem:password -p 443 tun1 192.168.4.1 &
tun=1
while :
do
until ifconfig tun$tun >/dev/null 2>&1
do
    sleep 10
done
route add 192.168.$sn.2 gw 192.168.$sn.1
#sn=`expr 3 + $tun`
#route add -net 192.168.$sn.0 netmask 255.255.255.0 gw 192.168.$sn.1
#if /sbin/iptables -L -v | grep tun$tun
#then
#    :
#else
#/sbin/iptables -A FORWARD -o tun$tun -j ACCEPT
#/sbin/iptables -A FORWARD -i tun$tun -j ACCEPT
#fi
tun=`expr $tun + 1`
done
