#!/usr/bin/env python

import sys, time, struct

def disable_cstates():
    f = open("/dev/cpu_dma_latency", "wb")
    f.write(struct.pack("i", 0))
    f.flush()
    while True:
        time.sleep(10)

disable_cstates()


