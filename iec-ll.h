#ifndef IEC_LL_H
#define IEC_LL_H

#define set_atn(state)   do { if (state) IEC_DDR &= ~IEC_BIT_ATN;   else IEC_DDR |= IEC_BIT_ATN;   } while(0)
#define set_data(state)  do { if (state) IEC_DDR &= ~IEC_BIT_DATA;  else IEC_DDR |= IEC_BIT_DATA;  } while(0)
#define set_clock(state) do { if (state) IEC_DDR &= ~IEC_BIT_CLOCK; else IEC_DDR |= IEC_BIT_CLOCK; } while(0)
#define set_srq(state)   do { if (state) IEC_DDR &= ~IEC_BIT_SRQ;   else IEC_DDR |= IEC_BIT_SRQ;   } while(0)

#define toggle_srq() IEC_PIN |= IEC_BIT_SRQ

#define IEC_ATN   (IEC_PIN & IEC_BIT_ATN)
#define IEC_DATA  (IEC_PIN & IEC_BIT_DATA)
#define IEC_CLOCK (IEC_PIN & IEC_BIT_CLOCK)
#define IEC_SRQ   (IEC_PIN & IEC_BIT_SRQ)

#define set_atnack(state) do { if (state) TIMSK2 |= _BV(OCIE2A); else TIMSK2 &= ~_BV(OCIE2A); } while(0)

#endif
