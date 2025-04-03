#ifndef _SHA_H_
#define _SHA_H_ 1

/* The structure for storing SHS info */

typedef struct 
{
	unsigned long int digest[ 5 ];            /* Message digest */
	unsigned long int countLo, countHi;       /* 64-bit bit count */
	unsigned long int data[ 16 ];             /* SHS data buffer */
	int Endianness;
} SHA_CTX;

/* Message digest functions */

void SHAInit(SHA_CTX *);
void SHAUpdate(SHA_CTX *, unsigned char *buffer, int count);
void SHAFinal(unsigned char *output, SHA_CTX *);

#endif /* end _SHA_H_ */
