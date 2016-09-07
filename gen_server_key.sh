#!/bin/bash
# Script to generate a valid certificate for use with sslserv
# Get an RSA key
openssl genrsa -des3 -passout pass:x -out server.pass.key 2048
openssl rsa -passin pass:x -in server.pass.key -out server.key
rm server.pass.key
# Certificate signing request
openssl req -new -key server.key -out server.csr
# Sign it.
echo Note the password, because you will need it to use this. The scripts assume password is password
openssl x509 -req -days 365 -in server.csr -signkey server.key >server.crt
cat server.key server.crt >server.pem
rm server.key server.crt server.csr
echo The file you need is server.pem
exit
# For apache, the bits go in separate files
# Generate private key for our Certificate Authority 
openssl genrsa -out ca.key 2048 

# Generate CSR 
openssl req -new -key ca.key -out ca.csr

# Generate Self Signed Key
openssl x509 -req -days 365 -in ca.csr -signkey ca.key -out ca.crt

# Copy the files to the correct locations
cp ca.crt /etc/pki/tls/certs
cp ca.key /etc/pki/tls/private/ca.key
cp ca.csr /etc/pki/tls/private/ca.csr
