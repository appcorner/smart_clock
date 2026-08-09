// เดิม guard เช็ค __AVR__ แต่บน ESP8266 core (Arduino.h) ประกาศ PROGMEM/memcpy_P/
// pgm_read_word ไว้แล้วจริง (ผ่าน pgmspace.h) ทำให้ #define ซ้ำตรงนี้ชนกัน
// (ค่าไม่เหมือนกัน เพราะของ ESP8266 มี __attribute__ ผูก section จริง ส่วนอันนี้เป็นค่าเปล่า)
// เปลี่ยนมาเช็คว่า PROGMEM ถูก define ไว้ก่อนหน้านี้แล้วหรือยัง (จาก Arduino.h) ถ้ามีแล้วก็ข้าม
// สำคัญ: __LPM ไม่ใช่ของ ESP8266 core เลย (เป็น macro เฉพาะ AVR) ต้อง define เอง
// เสมอบนแพลตฟอร์มนี้ แยกออกจากบล็อก PROGMEM/memcpy_P ที่ core มีให้แล้ว
// ไม่งั้น glog()/gexp()/ismasked() ใน qrencode.c จะคอมไพล์ไม่ผ่าน (__LPM undefined)
#ifndef __AVR__
#ifndef PROGMEM
#define PROGMEM
#define memcpy_P memcpy
#define pgm_read_word(x) *x
#endif
#ifndef __LPM
#define __LPM(x) (*(x))
#endif
#else
#include <avr/pgmspace.h>
#define USEPRECALC
#endif

extern unsigned char strinbuf[];
extern unsigned char qrframe[];

extern unsigned char  WD, WDB;
#include "qrbits.h"

#ifdef __cplusplus
extern "C"{
#endif
// strinbuf in, qrframe out
void qrencode(void);
#ifdef __cplusplus
} // extern "C"
#endif


