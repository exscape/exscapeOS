#ifndef MD5_H
#define MD5_H

#include <stdint.h>
#include <sys/types.h>
#undef HIGHFIRST

struct MD5Context {
        uint32 buf[4];
        uint32 bits[2];
        unsigned char in[64];
};

void MD5Init(struct MD5Context *ctx);
void MD5Update(struct MD5Context *ctx, unsigned char *buf, unsigned len);
void MD5Final(unsigned char digest[16], struct MD5Context *ctx);
void MD5Transform(uint32 buf[4], uint32 in[16]);

/*
 * This is needed to make RSAREF happy on some MS-DOS compilers.
 */
typedef struct MD5Context MD5_CTX;

/*  Define CHECK_HARDWARE_PROPERTIES to have main.c verify
    byte order and uint32 settings.  */
#define CHECK_HARDWARE_PROPERTIES

#endif /* !MD5_H */
