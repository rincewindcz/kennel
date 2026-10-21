#!/bin/sh
# goodluckOS HOME launcher (/home/player/.local/launcher.sh), started by appd as player.
# Runs kennel; when it fails (non-zero exit), Puppy takes over so the unit keeps a launcher.
/home/player/ports/kennel/kennel || exec /usr/bin/puppy
