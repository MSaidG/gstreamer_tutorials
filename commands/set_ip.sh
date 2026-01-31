#!/bin/bash

sudo ip addr flush dev eno1
sudo ip addr add 192.168.10.1/24 dev eno1
sudo ip link set eno1 up