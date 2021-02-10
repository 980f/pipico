//
// Created by andyh on 2/5/21.
//

#pragma once

#include "index.h"
#include "buffer.h"
#include "bitbanger.h"


namespace PicoPio {

  enum {
    ProgramMaxLength=32
  };
  /** This class holds the values that generate the configuration settings and which also modify how some instructions are encoded.
   * Initially it contains a program image as well as a cheap workaround for the requirement that all writes be 32 bit.
   *
   * The most abysmal thing it does, in order to not require a heap, is presume that all const char * values (program labels) are in the same 2 megabytes of memory.
   * This allows the 5 address bits of a jump and the 16 unused bits of every instruction to be a 21 bit address in the 32 bit actual address space.
   *
   * For the RP2040 a program can be split between RAM and XIP. The first symbol encountered will select the space.
   * */


  class Program {
    struct Field {
      unsigned base;
      unsigned width;
    };

    /** this feeds the sideset config. */
    struct SideSetter {
      bool withEnable;
      Field gpio;
    };

//bits set on most cycles
    SideSetter sideSetter;
//bits set by OUT
    Field output;
//bits read by IN
    Field input;

    Index wrapTarget;
    Index wrapLoop;

    unsigned SIDE_SET_COUNT=3; //placeholder for control register not yet defined


    //the actual program, upper 16 bits not used but hardware wants 32 bit writes so we allocate them all.
    unsigned code[ProgramMaxLength];


    class Location {
    public:
      Index address;
      const char *label;
    };

    /** the address is of the instruction JMP reference, and the index of that JMP reference will be that of a prior reference if multiple */
    Location symbols[ProgramMaxLength];

    Index ip;
    Index instruction;
    void merge(unsigned pattern,unsigned msb,unsigned lsb) {
      mergeField(instruction.raw, pattern, msb,lsb);
    }

  protected:
    /** called by functions that actually instantiate an instruction */
    bool newop(unsigned baseopcode) {
      if(instruction.isValid()) {
        if(ip.isValid()) {
          code[ip]=instruction;
        }
      }
      //an error if ip is invalid?
      instruction=baseopcode;
      ++ip;
      return ip<ProgramMaxLength;
    }
    /** side set of instruction being assembled */
    Program &side(unsigned pattern) {
      //todo: test side_set_count>0
      //first cut insist on writing to a buffer first so that we aren't bit by "must force 32 bit operation" if we directly manipulate a program line

      if(sideSetter.withEnable) {
        setBit(instruction.raw,12);
        //and at most 4 bits are 'side' pattern
        mergeField(instruction.raw, pattern,11,8+5-(SIDE_SET_COUNT-1));
      } else {
        mergeField(instruction.raw, pattern,12,8+5-SIDE_SET_COUNT);//at most 5 bits of side data, and if you have forgotten to spec a field then all bits get cleared!
      }
      return *this;
    }

    /** delay of opcode being assembled */
    Program &delay(unsigned pattern) {
      //todo: test side_set_count<5
      merge(pattern,12-SIDE_SET_COUNT,8);
      return *this;
    }

    enum SetCode {
      Pins=0,
      X=1,
      Y=2,
      Nuller=3,
      PinDirs=4,
      PC=5,
      ISR=6,
      OSR=7,
      Exec=7, //but will use a dedicate function rather than mae the user apply a SetCode
    };

    enum MovOp {
      Normal,
      Invert,
      Reverse,

    };

    /** Identifies subsequent operation (function call) as location that will be looped to */
    Program &Begin() {
      //todo: check not isValid? else two loops defined and that don't work
      wrapTarget=ip;//todo: OBO?
      return *this;
    }

    Program &Loop() {
      //todo: check not isValid?
      wrapLoop=ip;//todo: OBO?
      return *this;
    }

    Program&End() {
      newop(~0);
      return *this;
    }

  protected:
    Program &SOI(unsigned baseop,SetCode setCode,unsigned pattern) {
      newop(baseop);
      merge(setCode,7,5);
      merge(pattern,4,0);
      return *this;
    }

  public:
    /** bits from OSR go to @param setCode */
    Program &OUT(SetCode setCode,unsigned numbits) {
      //todo: report on numbits out of range.
      //todo: report on invalid setCode
      return SOI(0x6000,setCode, numbits);
    }

    /** bits from @param setCode are shifted into ISR */
    Program &IN(SetCode setCode,unsigned numbits) {
      //todo: report on numbits out of range.
      //todo: report on invalid setCode
      return SOI(0x4000,setCode, numbits);
    }

    Program &SET(SetCode setCode,unsigned pattern) {
      //todo: report on pattern out of range.
      return SOI(0xE000,setCode,pattern);
    }


    Program &MOV(SetCode from, SetCode to, unsigned movop) {
      newop(0xA000);
      merge(to,7,5);
      merge(movop,4,3);
      merge(from,2,0);
    }

  public:
    Program &copy(SetCode from, SetCode to) {
      return MOV(from,to,0);
    }
    Program &invert(SetCode from, SetCode to) {
      return MOV(from,to,1);
    }
    Program &bitrev(SetCode from, SetCode to) {
      return MOV(from,to,2);
    }

    Program& x(unsigned pattern) {
      return SET(X,pattern);
    }

    Program& y(unsigned pattern) {
      return SET(Y,pattern);
    }

    Program & PushPull(bool pullElsePush, bool always = false, bool blocking = true) {//defaults chosen to match picoasm
      newop(pullElsePush?0x8080:0x8000);
      merge(always,6,6);
      merge(blocking,5,5);
      return *this;
    }

    Program &PULL(bool always = false, bool blocking = true) {
      return PushPull(true, always, blocking);
    }

    Program &PUSH(bool always = false, bool blocking = true){
      return  PushPull(false, always, blocking);
    }



    Program &JMP(unsigned condition,const char *target){
      newop(0);
      merge(condition,7,5);
//      merge(pattern,4,0);
    }

    Program &DJNZ(SetCode setCode,const char *target){

      return *this;
    }

//        //returns value to pass to next invocation of load if using multiple proglists, after checking it for overflow!
//        unsigned load(Index location, unsigned *binary, const Instruction *proglist, unsigned proglength) {
//          const char *labelArg = nullptr;//gets applied to next op if not a directive
//          for (unsigned i = 0; i < proglength; ++i) {
//            auto &op = proglist[i];
//            switch (op.opcode) {
//              case WRAPREF:
//                wrapLoop = location;//perhaps -1
//                continue;
//                case WRAPDEF:
//                  wrapTarget = location;
//                  continue;
//                  case LABEL:
//                    //todo: record symbol
//                    continue;
//                  case opcJMP:
//                    //record reference
//                    break;
//                  //default: //emit instruction
//            }
//            if (location >= 32) {
//              return -1;//program space overflow
//            }
//            binary[location] = op.compile(location);
//            label[location++] = take(labelArg);
//          }
//          //now must resolve forward references, replacing JMP instruction's index which set to its own location as link to which symbol is referenced.
//          for (unsigned i = 0; i < proglength; ++i) {
//            const Instruction &op(proglist[i]);
//            if (op.opcode == opcJMP) {
//
//              const JMP &jmp( pun(const JMP,op));
//              for (unsigned li = countof(label); li-- > 0;) {
//                if (li != jmp.index) {
//                  if (label[li] == label[op.address]) {
//                    *binary[op.index] &= ~0x1F;
//                    *binary[op.index] &= li;;
//                  }
//                }
//              }
//            }
//          }
//
//          return location;//size of program
//        }
//
//      }};
//} //end namespace
//
//#if 0
//Functional approach, templated
//mass of
//constructors
//
//class Instruction {
//  enum Opcode {//using fixed parts of instruction patterns, with out of range high bit set for noncode operations.
//    JMP = 0
//    , WAIT = 0x2000
//    , IN = 0x4000
//    , OUT = 0x6000
//    , PUSH = 0x8000
//    , PULL = 0x8080
//    , MOV = 0xA000
//    , IRQ = 0xC000
//    , SET = 0xE000
//    , };
//  Instruction(Opcode,);
//}
//}
//#endif
