#!/bin/bash
if [ $# -lt 1 ]
then
    echo Provide an IP address
    exit
fi
ip=$1
cat << EOF >getcert.msg
\\H:1:client.pem:password\\
\\E:$ip:443:$ip:443:L:1\\
\\E:127.0.0.1:10114:127.0.0.1:10114:C\\
\\M:127.0.0.1;10114:$ip;443\\
\\D:B:127.0.0.1;10114:$ip;443\\
GET https://$ip/ HTTP/1.1
Host: $ip:443
User-Agent: Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:31.0) Gecko/20100101 Firefox/31.0
Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8
Accept-Language: en-US,en;q=0.5
Connection: keep-alive


\\D:E\\
\\X:127.0.0.1;10114:$ip;443\\
\\D:R\\
EOF
./t3drive -e tunsettings.sh -l 10.200.34.75:8080 -v2 fred 1 1 1 getcert.msg >getcert.log 2>&1
