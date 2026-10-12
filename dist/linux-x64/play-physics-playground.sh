#!/bin/sh
# Plays the sample game. Run from this folder: ./play-physics-playground.sh
cd "$(dirname "$0")" && exec ./LumenRuntime PhysicsPlayground "$@"
