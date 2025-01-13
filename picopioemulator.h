#pragma once
/**
// Created by andyh on 1/10/25.
// Copyright (c) 2025 Andy Heilveil, (github/980f). All rights reserved.
*/

#pragma once
#include <bitbanger.h>
#include <dout.h>  //GPIO pins?

class PicoPIOemulator {
public:
  bool V1 = false; //allow dynamic switching of architecture.

  struct BitSpan {
    unsigned lsb;
    unsigned count;
  };

  class SM {
    friend class PicoPIOemulator;
    PicoPIOemulator &group;
    unsigned ownIndex = ~0; //blow hard if not explicitly initialized
    unsigned long clockCounter = 0; //for cycle time estimates and coordinating external stimuli and output tracing

    SM(PicoPIOemulator *group, unsigned yourIndex) : group{*group}, ownIndex{yourIndex}, reg(cfg) {}

    struct Fifo {
      unsigned mem[8]; //allocate max
      unsigned memSize = 4; //defaults to 4, can also be 0 or 8
      unsigned readpointer = 0;
      unsigned writepointer = 0;
      unsigned quantity = 0;
      /* @whether overflowed if tx or underflowed if RX*/
      bool oopsed = false;

      bool isFull() {
        return quantity == memSize;
      }

      bool isEmpty() {
        return quantity == 0;
      }

      void push(unsigned value);

      unsigned pull();

      unsigned available() {
        return quantity;
      }

      void setSize(unsigned size);
    } RX, TX;

    /** driver can push instructions at the device. This is checked at the start of every instruction execution.*/
    struct InstructionInjector {
      uint16_t IR = 0;
      bool haveInjection = false;

      /** record the instruction and set a flag to indicate it overrides the instruction memory. */
      void operator()(uint16_t opcode) {
        //todo: mutex here, like java synchronized
        haveInjection = true;
        IR = opcode;
      }
    } inject;

    struct GPIO {
      unsigned pin = 0;
      unsigned dir = 0;

      unsigned input(BitSpan span);

      void output(BitSpan pins, unsigned value);

      void outdir(BitSpan pins, unsigned value);
    } gpio;

    /** for our first cut at it we are not worrying about the packing into control register words */
    struct Config {
      bool stalled = 0; //unclear whether this is control or status.

      unsigned jmpPin = 0;

      struct FstatTest {
        unsigned threshold = 0;
        bool testRx = 0; //else tx
        unsigned operator()(unsigned rx, unsigned tx) {
          return (testRx ? rx : tx) < threshold ? 0 : ~0; //RTFM
        }
      } ftest;

      unsigned starOfLoop = 0;
      unsigned endOfLoop = 31;

      bool outIsLatched = 0;
      bool inIsLatched = 0;
      unsigned OEpin = 0;

      struct SideSet {
        bool OEsnotPins = false;
        bool useEnable = false;
        BitSpan pins;
      } sideset;

      struct ShiftControl {
        /** value of zero is treated as 32 */
        unsigned threshold = 0;
        bool autoRefresh = false; //auto pull on OSR going empty
        bool shiftRight = false; //todo: check manual for reset state
        BitSpan pins;
      };

      ShiftControl IN;
      ShiftControl OUT;

      bool allFifosTX = false;
      bool allFifosRX = false;

      BitSpan set;

      unsigned nyi = ~0; //for things we don't yet know.
      unsigned getDelay(unsigned ir);
    } cfg;

    class ShiftRegister {
      uint32_t SR = 0;
      static constexpr int size = std::numeric_limits<decltype(SR)>::digits;
      unsigned consumed = 0;

      void bumpCount(unsigned numBits);

      Config::ShiftControl &cfg; //part of configuration register

    public:
      explicit ShiftRegister(Config::ShiftControl &cfg) : cfg{cfg} {}

      /** load all bits and clear cumulative shift count */
      void load(uint32_t value);

      /** @returns whether at least @param numBits has been shifted since the last load.
       * The user can confuse themselves by having the threshold not be a multiple of the bit quantities being moved by input/output.
       */
      bool isConsumed() const;

      /** modify SR with incoming bit field */
      void input(unsigned numBits, bool fromMsb, unsigned source);

      /** return shifted out stuff */
      unsigned output(unsigned numBits, bool fromMsb);

      unsigned all() {
        return SR;
      }

      void reset();
    };

    struct Registers {
      //the following are definitley unsigned, a signed int will not work!
      unsigned X = 0;
      unsigned Y = 0;
      ShiftRegister inputSR;
      ShiftRegister outputSR;
      unsigned PC = 0;
      unsigned IR = 0; //separate from what pico writes to so that we can use two actual threads for emulation and get real realtime troubles.
      unsigned opField(unsigned msb, unsigned lsb);

      unsigned opBit(unsigned msb);


      unsigned clock = 0; //diagnostic, for us to count instruction cycles and annotate output traces
      Registers(Config &cfg): inputSR{cfg.IN}, outputSR{cfg.OUT} {}
    } reg;

    /////////////////////////////
    enum Condition {
      Always = 0,
      Xzero,
      Xnzd,
      Yzero,
      Ynzd,
      XneY,
      Pin,
      OSRne,
    };

    void InternalError() {
      //emulator has goofed up!
    }

    void IllegalOP(bool dueToConfig) {
      //unimplemented opcode pattern, or otherwise officially "undefined operation"
    }

    void sideset(unsigned opfield);

    /** @returns whether to jump, including doing the decrements on scratch registers. */
    bool testCondition(Condition condition);

    void JMP(Condition condition, unsigned target);

    void IN(unsigned source, unsigned bitCount);

    /** ISR to RX fifo */
    void PUSH(bool ifFull, bool block);

    /** TX fifo to OSR */
    void PULL(bool ifEmpty, bool block);

    void WAIT();

    /** from instruction to pins */
    void SET() {}

    /** copy from one place to another conditionally transforming the data  */
    void MOV(unsigned target, bool reverse, bool invert, unsigned source);

    /** bits from OSR go somewhere, and are removed form OSR */
    void OUT(unsigned opoptions, unsigned indexCount);

    /////////////////////////////////////////////
    /** whether this SM is running */
    bool simulating;
    /* clear all sorts of internals, a reset*/
    void restart();
    void run();

    void simulatedClock() {
      //todo: merge gpio output sources according to priority. sideset> (OUT, SET) conflicts not specified
      //todo: yield via minimal OS wait()
      // Anything altering pins or irq or config needs to wake up this thread.
      //if not multi-threading then this is a call to input + host simulation logic to which the clockCounter value is passed, and in increment returned.
      ++clockCounter;
    }

    void stall(std::function<bool()> condition);

    void irqWait(unsigned which, bool toBeSet);

    void txWait();

    void rxWait();

    void pinWait(unsigned bitnum, bool toBeSet);

    void IRQ(unsigned indexCount);
  }; //end SM
  /////////////////////////////////////////////////////////////////////////////////////////////////
  //back to PIO itself
#define numSMs 4
  SM sm[numSMs] = {SM(this, 0), SM(this, 1), SM(this, 2), SM(this, 3)};
  static const unsigned SMsize = numSMs;

  struct IRQstuff {
    /** so many parallel operations that we are going to bit twiddle rather than structure */
    /* this should be RO from the host: */
    unsigned flags = 0; //INTR, the sources
    /* this should be masked to 12 bits on writes */
    unsigned forces[2]; //IRQ[0,1]_INTF
    unsigned enables[2]; //IRQ[0,1]_INTE

    /* for the IRQ instruction */
    void irqOp(unsigned bitnum, bool value) {
      assignBit(flags, bitnum + 8, value);
    }

    /* fifos must report to here */
    void fifoEvent(unsigned whichSM, bool RX) {
      assignBit(flags, whichSM + (RX ? 0 : 4), RX);
    }

    /** @returns the interrupt status, if not zero then the IRQ would be active */
    unsigned INTS(bool whichof2) {
      return (flags | forces[whichof2]) & enables[whichof2];
    }

    bool is(unsigned which, bool toBeSet) {
      return bit(flags,which+8)==toBeSet;
    }
  } irq;

  /** base for implementing pack/unpack from memory mapped values to easy to work with objects. */
  struct PsuedoMemory {
    PicoPIOemulator &group;
    PsuedoMemory(PicoPIOemulator *super): group{*super} {}
    uint32_t image = 0;

    virtual operator uint32_t() {
      return image;
    }

    virtual void operator =(uint32_t pattern) {
      image = pattern;
    }
  };

  template<typename Bitly> class Packed : PsuedoMemory, Bitly {
    virtual operator uint32_t() {
      Bitly *caster(this);
      image = *reinterpret_cast<unsigned *>(caster);
      return image;
    }

    void operator =(uint32_t pattern) {
      image = pattern;
      *reinterpret_cast<Bitly *>(&image) = pattern;
    }
  };

public: //creating logical interface, bypassing the memory mapped interface

  struct ControlWord : PsuedoMemory {
    //simulating bits, internal hardware resets, clock synch
    ControlWord(PicoPIOemulator *super) : PsuedoMemory(super) {}
  } theCTRL;

  /* bridge to simulating memory mapped access
   * implements FSTAT FDEBUG FLEVEL TXF0..3 RXF0..3
   */
  class FifoAccessor {
    SM::Fifo &theFifo; //= RX?theSM.RX:theSM.TX;
    const bool RXer; //for convenience of host interface, to enforce read-only or write-only from there. Is R-W to emulator.

  public:
    struct Bits {
      bool isEmpty = false;
      bool isFull = false;
      unsigned level = 0; // 0 to 4 or 8, 4 it field
      bool stalled = false; //whether we are waiting on this, and a clear of it stops the wait although sending data should do that as well.
      bool overflow = false; //todo: NYI but will be easy.
    };

    FifoAccessor(PicoPIOemulator &super, unsigned smIndex, bool RX): theFifo(RX ? super.sm[smIndex].RX : super.sm[smIndex].TX), RXer{RX} {}

    Bits status() {
      Bits packer;
      packer.isEmpty = theFifo.isEmpty();
      packer.isFull = theFifo.isFull();
      packer.level = theFifo.available();
      packer.stalled = false; //not accessible until we multithread each sm separate
      packer.overflow = theFifo.oopsed;
      return packer;
    }

    //read from fifo to host,
    operator unsigned() {
      if (!RXer) {
        //todo: officially unspecified behavior
        return ~0; //return a marker for now, should simulate a memory fault
      }
      return theFifo.pull();
    }

    //write to fif0 from host
    void operator=(unsigned value) {
      if (RXer) {
        //todo: officially unspecified behavior
      } else {
        theFifo.push(value);
      }
    }

    void clearStall() {
      //NYI, need ot break a wait
      // theFifo.stalled=false;
    }

    void clearOops() {
      theFifo.oopsed = false;
    }
  } fife;

  unsigned FSTAT_pack();

  void Fdebug_parse(unsigned pattern);

  unsigned FLevel_pack();

  /** manages IRQ wc, IRQ_FORCE  ,and then INTR ro IRQ[0:1][_INTE _INTF _INTS]
   * the latter 3 list 12 items, 8 of which make sense, 4 of which are totally undocumented, unless they are the lower 4 of the 8!.
   * will use IRQstuff, mostly 1:1
   */
  class InterruptAccessor {
    PicoPIOemulator &group; //where the bits and fifos reside
  public:
    InterruptAccessor(PicoPIOemulator &group) : group{group} {}
  };
  uint16_t mem[32];

  // struct InstructionMemory : PsuedoMemory {
  //   unsigned &address;
  //   operator unsigned int() override {
  //     return mem[address];
  //   }
  //
  //   void operator=(uint32_t pattern) override;
  // };

public:

  //tokens for memory mapped access by host
  enum class SM_CBfield { //~6 words for each SM
    CLKDIV = 0, EXECCTRL, SHIFTCTRL, ADDR, INSTR, PINCTRL,
    BlockSize
  };

  /** the read device's host interface is memory mapped so we only offer a similar interface for simulation.
   * The expectation is that we will write an actual class for interfacing to the whole PIO that uses the following function. If that gets too tedious we'll just directly declare convenient interface methods then implement one with memory accesses.
   */
  enum RegisterName {
    CTRL = 0,
    FSTAT, //full and empty bits for each SM's fifos
    FDEBUG, //stalls and overflows, host can clear stall although pushing or pulling data would seem more appropriate
    FLEVEL, //fifo levels for each fifo
    TXfifo, //push/pull access to each SM's tx fifo
    RXfifo = TXfifo + numSMs,
    IRQ = RXfifo + numSMs, IRQ_FORCE,
    //gpio intereface:
    INPUT_SYNC_BYPASS, DBG_PADOUT, DBG_PADOE, DBG_CFGINFO,
    INSTR_MEM,
    SM_cb,
    INTR = SM_cb + numSMs * unsigned(SM_CBfield::BlockSize),
    IRQ0_EFS,
    IRQ1_EFS = IRQ0_EFS + 3,
  };

  /* need map of memory address to read/write handler.
   * so each handler extends PseudoMemory which emulates array of unsigneds, not a single one.
   * each PSM's has base address and length.we std::vector them, class Chained?
   * the PSM methods of the PIO iterate over the table of PSM's and assignto or convert from the object using the unreduced address.
   *
   */

  // PsuedoMemory NYI;
  //
  // PsuedoMemory &operator[](unsigned index) {
  //   if (index >= IRQ1_EFS)       return NYI;
  //   if (index >= IRQ0_EFS)       return NYI;
  //   if(index== INTR) return NYI;
  //   if (index>=SM_cb) return NYI;
  //   if (index>=INSTR_MEM) return &mem[index-INSTR_MEM];
  //   return theCTRL;
  // }
};
