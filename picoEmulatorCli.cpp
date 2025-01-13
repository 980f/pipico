/**
// Created by andyh on 1/13/25.
// Copyright (c) 2025 Andy Heilveil, (github/980f). All rights reserved.

THis is mostly just to get compiles on the pico pio emulator module.
It will perhaps grow over time to feed a timing generator into the emulator and to trace the patterns out.

*/

#include "picopioemulator.h"

int main(int argc,char *argv[]) {
  PicoPIOemulator pio0(0,false);
  pio0.theCTRL=1;
}
