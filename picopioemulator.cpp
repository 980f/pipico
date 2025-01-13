/**
// Created by andyh on 1/10/25.
// Copyright (c) 2025 Andy Heilveil, (github/980f). All rights reserved.
*/

#include "PicoPIOemulator.h"
#include <bitbanger.h>
#include <bit> //rotate instructions are still new to C++, despite being present in nearly every microcomputer ever, at least since hte early 1970's when C was invented.

void PicoPIOemulator::SM::Fifo::push(unsigned value) {
  if (isFull()) {
    oopsed |= true;
  } else {
    mem[++writepointer] = value;
    ++quantity;
  }
}

unsigned PicoPIOemulator::SM::Fifo::pull() {
  if (isEmpty()) {
    oopsed |= true;
    return mem[readpointer]; //guessing at what hardware does, caller should check isEmpty before pull'ing.
  } else {
    --quantity;
    return mem[readpointer++];
  }
}

void PicoPIOemulator::SM::Fifo::setSize(unsigned size) {
  if (changed(memSize, size)) {
    //forget the content.
    quantity = 0;
    readpointer = writepointer = 0;
    //but we won't bother clearing the memory itself.
  }
}

unsigned PicoPIOemulator::SM::GPIO::input(BitSpan span) {
  //actually had to be a right rotate,the lsbs normally discarded go to the msbs.
  return std::rotr(pin, span.lsb) & fieldMask(span.count);
}

void PicoPIOemulator::SM::GPIO::output(BitSpan pins, unsigned value) {
  //todo: theoretically we might write to the lsbs of gpio with a numbits+lsb exceeds 32, but there are 2 not implemented bits, 30,31, making that useless.
  insertField(pin, value, pins.lsb + pins.count - 1, pins.lsb);
}

void PicoPIOemulator::SM::GPIO::outdir(BitSpan pins, unsigned value) {
  insertField(dir, value, pins.lsb + pins.count - 1, pins.lsb);
}

void PicoPIOemulator::SM::ShiftRegister::load(uint32_t value) {
  SR = value;
  consumed = 0;
}

bool PicoPIOemulator::SM::ShiftRegister::isConsumed() const {
  return cfg.threshold ? consumed >= cfg.threshold : consumed >= 32;
}

unsigned PicoPIOemulator::SM::Config::getDelay(unsigned ir) {
  unsigned msb = 12 - sideset.pins.count;
  return extractField(ir, msb, 8);
}

void PicoPIOemulator::SM::ShiftRegister::bumpCount(unsigned numBits) {
  consumed += numBits;
  if (consumed > 32) consumed = 32; //claim is this saturates, probably sets a "full flag" and refuses operations.
}

void PicoPIOemulator::SM::ShiftRegister::input(unsigned numBits, bool fromMsb, unsigned source) {
  if (numBits == 0) { //means 32
    SR = source;
    consumed = 32;
    return;
  }
  if (fromMsb) {
    SR >>= numBits;
    SR |= (source & fieldMask(numBits, 0)) << (size - numBits);
  } else {
    SR <<= numBits;
    SR |= source & fieldMask(numBits, 0);
  }
  bumpCount(numBits);
}

unsigned PicoPIOemulator::SM::ShiftRegister::output(unsigned numBits, bool fromMsb) {
  if (numBits == 0) {
    consumed = 32;
    return take(SR);
  }
  //todo: what do we do if more bits than are present is asked for? My guess is shift as commanded and let
  unsigned target = 0;
  if (fromMsb) {
    target = SR & fieldMask(numBits, 0);
    SR >>= numBits;
  } else {
    target = SR & fieldMask(size, size - numBits);
    SR <<= numBits;
  }
  bumpCount(numBits);
  return target;
}

void PicoPIOemulator::SM::ShiftRegister::reset() {
  SR=0;
  consumed=0;

}

unsigned PicoPIOemulator::SM::Registers::opField(unsigned msb, unsigned lsb) {
  return extractField(IR, msb, lsb);
}

unsigned PicoPIOemulator::SM::Registers::opBit(unsigned msb) {
  return bit(IR, msb);
}

void PicoPIOemulator::SM::sideset(unsigned opfield) {
  bool hasEnable = cfg.sideset.useEnable;
  if (hasEnable) {
    if (!bit(opfield, 4)) {
      return;
    }
  }

  if (cfg.sideset.pins.count > (hasEnable ? 4 : 5)) {
    IllegalOP(true); //and if user doesn't handle these they will get spammed with them!
    return;
  }
  int numDelayBits = 5 - hasEnable - cfg.sideset.pins.count;
  if (numDelayBits < 0) {
    IllegalOP(true);
    return;
  }

  auto fn = cfg.sideset.OEsnotPins ? &GPIO::outdir : &GPIO::output;
  (gpio.*fn)(cfg.sideset.pins, opfield >> numDelayBits);
}

bool PicoPIOemulator::SM::testCondition(Condition condition) {
  switch (condition) {
    case Always:
      return true;
    case Xzero:
      return reg.X == 0;
    case Xnzd:
      return reg.X-- > 0;
    case Yzero:
      return reg.Y == 0;
    case Ynzd:
      return reg.Y-- > 0;
    case XneY:
      return reg.X != reg.Y;
    case Pin:
      return bit(gpio.pin, cfg.jmpPin);
    case OSRne:
      return !reg.outputSR.isConsumed(); //not our job to worry about fractional last field?
    default:
      return false;
  }
}

void PicoPIOemulator::SM::JMP(Condition condition, unsigned target) {
  if (testCondition(condition)) {
    reg.PC = target;
  }
}

void PicoPIOemulator::SM::IN(unsigned source, unsigned bitCount) {
  unsigned incoming = 0;
  switch (source) {
    case 0:
      incoming = gpio.input(cfg.IN.pins);
      break;
    case 1:
      incoming = reg.X;
      break;
    case 2:
      incoming = reg.Y;
      break;
    case 3:
      incoming = 0;
      break;
    case 4:
      IllegalOP(false);
      return;
    case 5:
      IllegalOP(false);
      return;
    case 6:
      incoming = reg.inputSR.all();
      break;
    case 7:
      incoming = reg.outputSR.all();
      break;
    default:
      InternalError();
      return;
  }
  if (cfg.IN.shiftRight) {
    reg.inputSR.input(bitCount, cfg.IN.shiftRight, incoming);
  }
}

void PicoPIOemulator::SM::PUSH(bool ifFull, bool block) {
  if (ifFull) {}
  if (block) {
    if (RX.isFull()) {
      rxWait();
    }
  }
}

void PicoPIOemulator::SM::PULL(bool ifEmpty, bool block) {
  if (ifEmpty) {
    // if (reg.outputSR.isConsumed()) {
    //   //todo:some kind of wait
    // }
  }

  if (block) {
    if (TX.isEmpty()) { //while this is the same condition as is tested in txWait we don't want the extra clock that presently txWait and the like emit.
      txWait();
    }

    reg.outputSR.load(reg.X);
  } else {
    reg.outputSR.load(TX.pull());
  }

  if (TX.isEmpty()) {
    if (ifEmpty) {
      //officially do nothing
    }
    reg.outputSR.load(reg.X);
  } else {
    reg.outputSR.load(TX.pull());
  }
}

void PicoPIOemulator::SM::WAIT() {}

void PicoPIOemulator::SM::MOV(unsigned int target, bool reverse, bool invert, unsigned source) {
  unsigned incoming = 0;
  switch (source) {
    case 0:
      incoming = gpio.input(cfg.IN.pins); //todo: verify count source
      break;
    case 1:
      incoming = reg.X;
      break;
    case 2:
      incoming = reg.Y;
      break;
    case 3:
      incoming = 0;
      break;
    case 4:
      IllegalOP(false);
      return;
    case 5:
      incoming = cfg.ftest(RX.available(), TX.available());
      break;
    case 6:
      reg.inputSR.all();
      break;
    case 7:
      reg.outputSR.all();
      break;
    default:
      InternalError();
      return;
  }
  if (reverse && invert) {
    if (group.V1) {} else {
      IllegalOP(false);
      return;
    }
  }
  if (reverse) {
    //todo: bit reverse incoming
  }
  if (invert) {
    incoming = ~incoming;
  }
  switch (target) {
    case 0:
      gpio.output(cfg.OUT.pins, incoming); //todo: read manual
      break;
    case 1:
      reg.X = incoming;
      break;
    case 2:
      reg.Y = incoming;
      break;
    case 3:
      if (group.V1) {} else {
        IllegalOP(false);
        return;
      }
      break;
    case 4:
      inject(incoming);
    //todo: delay is not taken, but sideset executes.
      break;
    case 5:
      reg.PC = incoming;
      break;
    case 6:
      reg.inputSR.load(incoming);
      break;
    case 7:
      reg.outputSR.load(incoming);
      break;
    default:
      InternalError();
      return;
  }
}

void PicoPIOemulator::SM::OUT(unsigned int opoptions, unsigned int indexCount) {
  auto pattern = reg.outputSR.output(indexCount, cfg.OUT.shiftRight);
  if (cfg.OUT.autoRefresh && reg.outputSR.isConsumed()) {
    if (TX.isEmpty()) {
      txWait();
    }
    reg.outputSR.load(TX.pull());
  }
  switch (opoptions) {
    case 0:
      gpio.output(cfg.OUT.pins, pattern);
      break;
    case 1:
      reg.X = pattern;
      break;
    case 2:
      reg.Y = pattern;
      break;
    case 3: //no op other than shifting output SR
      break;
    case 4:
      gpio.outdir(cfg.OUT.pins, pattern);
      break;
    case 5:
      reg.PC = pattern;
      break;
    case 6:
      reg.inputSR.load(pattern);
      break;
    case 7:
      inject(pattern);
      break;
    default:
      InternalError();
      return;
  }
}

void PicoPIOemulator::SM::stall(std::function<bool()> condition) {
  cfg.stalled = true;
  while (condition()) {
    simulatedClock();
  }
  cfg.stalled = false;
}

void PicoPIOemulator::SM::irqWait(unsigned which, bool toBeSet) {
  stall([this,which,toBeSet] {
    return group.irq.is(which, !toBeSet);
  });
}

void PicoPIOemulator::SM::txWait() {
  stall([this] {
    return TX.isEmpty();
  });
}

void PicoPIOemulator::SM::rxWait() {
  stall([this] {
    return RX.isFull();
  });
}


void PicoPIOemulator::SM::pinWait(unsigned bitnum, bool toBeSet) {
  stall([this,bitnum,toBeSet] {
    return bit(gpio.pin, bitnum) != toBeSet;
  });
}

void PicoPIOemulator::SM::IRQ(unsigned indexCount) {
  //the following could be done in one hairy conditional, but that was hard to read and explain so we do it in baby steps
  unsigned irqnum = indexCount & 3; //starting with the 2 lsbs
  if (bit(indexCount, 4)) { //if the msb of the field is set
    irqnum += ownIndex; //then add ownIndex
    irqnum %= 4; //modulo 4 (compiler knows to and with 3)
  }
  irqnum |= bit(indexCount, 2) << 2; //bit 2 is always used as is.

  auto raise=reg.opBit(6);
  group.irq.irqOp( irqnum,raise);
  if (raise && reg.opBit(5)) {
    irqWait(irqnum, false);
  }
}

unsigned PicoPIOemulator::FSTAT_pack() {
  unsigned image = 0;
  for (unsigned i = SMsize; i-- > 0;) {
    SM &sm_ = sm[i];
    image |= sm_.RX.isFull() << (i + 0);
    image |= sm_.RX.isEmpty() << (i + 8);
    image |= sm_.TX.isFull() << (i + 16);
    image |= sm_.TX.isEmpty() << (i + 24);
  }
  return image;
}

void PicoPIOemulator::Fdebug_parse(unsigned pattern) {
  for (unsigned i = SMsize; i-- > 0;) {
    SM &sm_ = sm[i];
    if (bit(pattern,i+0)) {
      //todo: end a waitspin sm_.RX.unstall();
    }
    if (bit(pattern,i+0)) {
      sm_.RX.oopsed=0;
    }
    //# yes, stall vs oops is swapped between RX and TX, at least according to manual.
    if (bit(pattern,i+24)) {
      //todo: end a waitspin sm_.TX.unstall();
    }
    if (bit(pattern,i+16)) {
      sm_.TX.oopsed=0;
    }
  }
}

unsigned PicoPIOemulator::FLevel_pack() {
  unsigned image = 0;
  for (unsigned i = SMsize; i-- > 0;) {
    SM &sm_ = sm[i];
    image |= sm_.RX.available() << (8*i + 4);
    image |= sm_.TX.available() << (8*i + 0);
  }
  return image;
}

void PicoPIOemulator::SM::restart() {
  reg.inputSR.reset();

}

void PicoPIOemulator::SM::run() {
  simulating = true;
  while (simulating) {
    //first apply config
    RX.setSize(cfg.allFifosRX ? 8 : cfg.allFifosTX ? 0 : 4); //setSize does a change detect, we don't need to do that here
    TX.setSize(cfg.allFifosTX ? 8 : cfg.allFifosRX ? 0 : 4);

    if (flagged(inject.haveInjection)) { //we are supposed to clear haveInjection when the instruction is over, but doing it here works fine as we don't inspect it again until the instruction is over.
      //more on timing: clearing haveInjection indicates that the host can write a new instruction without perturbing the one in progress, and that is true at this point as the IR value they write is not the same memory location as what we use for opcode inspection.
      reg.IR = inject.IR;
      //and PC is untouched
    } else {
      reg.IR = group.mem[reg.PC]; //todo: move exec and external write to exec.
      //do PC adjustments before we potentially execute a JMP or MOV to PC
      if (reg.PC == cfg.endOfLoop) {
        reg.PC = cfg.starOfLoop;
      } else {
        ++reg.PC;
      }
    }
    sideset(reg.opField(12, 8)); //todo: side set needs to take priority over other gio setting by instructions, but must proceed waiting. The resolution will be to have multiple 'pin' fields in gpio and merge them via priority rules to get the value that we inform the outside world of with each simulated clock cycle.

    auto opoptions = reg.opField(7, 5);
    auto indexCount = reg.opField(4, 0);
    switch (reg.opField(15, 13)) {
      case 0:
        JMP(static_cast<Condition>(opoptions), indexCount);
        break;
      case 1: //wait
        if (reg.opBit(6)) { //an irq wait
        } else { //gpio wait
          if (reg.opBit(5)) {
            indexCount += cfg.IN.pins.lsb;
            indexCount %= 32;
          }
          pinWait(indexCount, reg.opBit(7));
        }
        WAIT();
        break;
      case 2: //in
        IN(opoptions, indexCount);
        break;
      case 3: //out
        OUT(opoptions, indexCount);
        break;
      case 4:
        if (opoptions & 3 == 0) {
          IllegalOP(false); //V1: RX as mailboxes
        } else {
          auto conditionally = reg.opBit(6);
          auto block = reg.opBit(5);
          if (reg.opBit(7)) {
            PULL(conditionally, block);
          } else {
            PUSH(conditionally, block);
          }
        }
        break;
      case 5:
        MOV(opoptions, bit(reg.IR, 4), bit(reg.IR, 3), indexCount & 7);
        break;
      case 6:
        IRQ(indexCount);
        break;
      case 7:
        SET();
        break;
      default:
        InternalError();
        break;
    }
    //delay comes last as it might get modified by instruction execution:
    unsigned delayCount = inject.haveInjection ? 0 : cfg.getDelay(reg.IR); //if we overwrite the IR while processing an instruction we ditch the delay from the original instruction but also do not use that of the injected one.
    do {
      simulatedClock(); //we do this once even if we are not going to delay, to inform the world of what the instruction did.
    } while (delayCount--);
  }
}
