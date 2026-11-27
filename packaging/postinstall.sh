#!/bin/sh

if [ -d /run/systemd/system ]; then
    systemctl daemon-reload || :
    systemctl try-restart beelink-me-mini-fand.service || :
fi
