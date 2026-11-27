#!/bin/sh

# dpkg passes "remove", RPM passes 0, and pacman passes pkgver-pkgrel.
case "$1" in
    remove|0|[0-9]*-*)
        if [ -d /run/systemd/system ]; then
            systemctl disable --now beelink-me-mini-fand.service || :
        fi
        ;;
esac
