#!/bin/bash -ev

mkdir -p ~/.pandacoin
echo "rpcuser=username" >>~/.pandacoin/pandacoin.conf
echo "rpcpassword=`head -c 32 /dev/urandom | base64`" >>~/.pandacoin/pandacoin.conf
