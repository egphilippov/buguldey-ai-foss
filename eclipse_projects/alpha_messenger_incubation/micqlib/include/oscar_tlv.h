/* $Id: oscar_tlv.h,v 1.1 2003/10/11 02:44:03 kuhlmann Exp $ */

#ifndef MICQ_OSCAR_TLV_H
#define MICQ_OSCAR_TLV_H

typedef struct { UWORD tag; UDWORD nr; str_s str; } TLV;

TLV  *TLVRead (Packet *pak, UDWORD TLVlen);
UWORD TLVGet  (TLV *tlv, UWORD nr);
void  TLVDone (TLV *tlv, UWORD nr);
void  TLVD    (TLV *tlv);

#endif
