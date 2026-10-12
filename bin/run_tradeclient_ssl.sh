#!/bin/sh
# Runnable from anywhere: the program and its cfg/ are beside this script.
cd "$(dirname "$0")" || exit 1
exec ./tradeclient cfg/tradeclient.cfg SSL
