cmake_minimum_required(VERSION 3.16)
project(StartupDisplayProbe LANGUAGES C)
include(add_application)
add_application(StartupDisplayProbe probe.c CREATOR "FwPr")
