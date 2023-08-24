#!/bin/sh

set -xe

cc -Wall -Wextra -o voronoi-ppm src/main_ppm.c
cc -Wall -Wextra -o voronoi-opengl src/main_opengl.c -lglfw3 -lGL -lm
