#ifndef _SHA_H_
#define _SHA_H_ 1

#include <inttypes.h>

/* The structure for storing SHS info */

typedef struct 
{
	uint32_t digest[ 5 ];            /* Message digest */
	uint32_t countLo, countHi;       /* 64-bit bit count */
	uint32_t data[ 16 ];             /* SHS data buffer */
	int Endianness;
} SHA_CTX;

/* Message digest functions */

void SHAInit(SHA_CTX *);
void SHAUpdate(SHA_CTX *, unsigned char *buffer, int count);
void SHAFinal(unsigned char *output, SHA_CTX *);

#endif /* end _SHA_H_ */
