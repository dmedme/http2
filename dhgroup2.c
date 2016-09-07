DH *get_dh512()
	{
	static unsigned char dh512_p[]={
		0x85,0xA8,0xE8,0x2C,0x02,0x67,0xAE,0xE5,0xDF,0x13,0x0E,0x8D,
		0xF8,0xFA,0xAD,0x7B,0xB6,0x65,0x36,0xBC,0x72,0xC9,0xE3,0x5A,
		0x52,0x69,0x77,0xC7,0xAE,0x51,0xC3,0xAA,0xAD,0xD8,0xE4,0x35,
		0xBA,0x75,0x64,0xE6,0xF0,0x1B,0xC2,0x26,0xB2,0x81,0xF0,0x28,
		0x79,0x30,0x08,0x7C,0x40,0x01,0x1A,0xA7,0x21,0xB1,0x5E,0x1D,
		0xE7,0xD4,0x8E,0x23,
		};
	static unsigned char dh512_g[]={
		0x02,
		};
	DH *dh;

	if ((dh=DH_new()) == NULL) return(NULL);
	dh->p=BN_bin2bn(dh512_p,sizeof(dh512_p),NULL);
	dh->g=BN_bin2bn(dh512_g,sizeof(dh512_g),NULL);
	if ((dh->p == NULL) || (dh->g == NULL))
		{ DH_free(dh); return(NULL); }
	return(dh);
	}
-----BEGIN DH PARAMETERS-----
MEYCQQCFqOgsAmeu5d8TDo34+q17tmU2vHLJ41pSaXfHrlHDqq3Y5DW6dWTm8BvC
JrKB8Ch5MAh8QAEapyGxXh3n1I4jAgEC
-----END DH PARAMETERS-----
#ifndef HEADER_DH_H
#include <openssl/dh.h>
#endif
DH *get_dh1024()
	{
	static unsigned char dh1024_p[]={
		0xAA,0xDE,0xCF,0xC0,0xB6,0x45,0xF7,0x9F,0x7A,0xBB,0x1B,0xD7,
		0xF8,0x35,0x18,0xA3,0x20,0x8B,0x60,0x0D,0x32,0xCB,0x85,0xFE,
		0xA6,0x4B,0x5A,0x63,0x35,0xC7,0x95,0xA7,0x2E,0xEB,0xDB,0x60,
		0xA1,0xC7,0xDD,0xC6,0x33,0xAC,0x2F,0x8D,0x4D,0xF6,0x51,0x03,
		0x23,0xE2,0xE4,0xE2,0x9E,0x21,0xC7,0xDE,0xA0,0xB6,0xCF,0x17,
		0xB8,0x14,0x98,0xE3,0xE1,0x4B,0xC6,0x7E,0x3F,0xC5,0xC0,0x57,
		0x57,0x72,0x9C,0x90,0x33,0x54,0x97,0xA7,0xDD,0x90,0x0A,0x6E,
		0xC7,0x76,0x0E,0xC7,0x60,0x9D,0x5B,0xED,0xBD,0xBA,0x4E,0x99,
		0x7F,0xD4,0x69,0xE9,0x06,0xED,0xBF,0x2F,0x2C,0x74,0x82,0xBF,
		0xB7,0xB4,0xDC,0x29,0xC9,0xEA,0x92,0x63,0xCD,0xB3,0x55,0xD6,
		0xFA,0x0A,0xA6,0x50,0x8A,0xE7,0x08,0x73,
		};
	static unsigned char dh1024_g[]={
		0x02,
		};
	DH *dh;

	if ((dh=DH_new()) == NULL) return(NULL);
	dh->p=BN_bin2bn(dh1024_p,sizeof(dh1024_p),NULL);
	dh->g=BN_bin2bn(dh1024_g,sizeof(dh1024_g),NULL);
	if ((dh->p == NULL) || (dh->g == NULL))
		{ DH_free(dh); return(NULL); }
	return(dh);
	}
-----BEGIN DH PARAMETERS-----
MIGHAoGBAKrez8C2Rfefersb1/g1GKMgi2ANMsuF/qZLWmM1x5WnLuvbYKHH3cYz
rC+NTfZRAyPi5OKeIcfeoLbPF7gUmOPhS8Z+P8XAV1dynJAzVJen3ZAKbsd2Dsdg
nVvtvbpOmX/UaekG7b8vLHSCv7e03CnJ6pJjzbNV1voKplCK5whzAgEC
-----END DH PARAMETERS-----
#ifndef HEADER_DH_H
#include <openssl/dh.h>
#endif
DH *get_dh2048()
	{
	static unsigned char dh2048_p[]={
		0xD2,0x45,0xFA,0xBF,0x4E,0x6B,0x80,0x4A,0x6E,0xBD,0x56,0xF4,
		0x50,0x7E,0xAB,0x78,0x1D,0xB7,0xCE,0x20,0x79,0xD8,0xA8,0x95,
		0xE3,0x19,0x53,0x58,0x8F,0xA8,0xF6,0x5A,0xFF,0x00,0xCF,0x93,
		0x60,0x69,0xC1,0x10,0xFC,0x8B,0x66,0x64,0x7D,0x63,0x2F,0x72,
		0x83,0x88,0x7B,0x45,0x36,0x05,0xB2,0x1F,0x8F,0xB6,0xEC,0xE7,
		0x1F,0x22,0x94,0xAF,0xAF,0x47,0xCC,0xBB,0x8A,0x2B,0xF4,0xEF,
		0xF4,0x93,0x7F,0xD5,0x63,0x09,0x80,0x2C,0xDB,0x00,0x39,0x76,
		0x54,0xE6,0x7D,0x62,0xBC,0xBA,0x00,0x98,0x00,0x31,0x77,0x5D,
		0xFB,0xBC,0x06,0x46,0x07,0x46,0x6B,0x4C,0x57,0xAE,0x3F,0x31,
		0x8A,0x6C,0xA2,0x7E,0x81,0x4C,0x00,0xF9,0x6C,0xC5,0x39,0x03,
		0x18,0x2C,0x3B,0x9F,0x28,0xCE,0x87,0x6A,0x52,0xA2,0x7C,0x92,
		0xCA,0x5C,0xA0,0x63,0x84,0xD0,0x42,0xD4,0xE1,0xBB,0x7A,0xAD,
		0xE0,0xB7,0xBC,0xCC,0x07,0x64,0x26,0x45,0x02,0x92,0xA2,0xE8,
		0x99,0x5A,0xF8,0xB8,0x4A,0xD3,0xB1,0xAD,0xD8,0x01,0xA9,0x80,
		0xB1,0x3D,0x14,0xBE,0x63,0x56,0x85,0x37,0xE1,0xB3,0x58,0x59,
		0x0F,0x3A,0x9D,0x5E,0xBF,0x75,0x9D,0xCB,0x7F,0xF8,0x2C,0x5D,
		0xBF,0xE1,0x98,0x89,0x77,0xF4,0xB2,0xCA,0xBA,0x46,0xA1,0x05,
		0x92,0x6E,0x3D,0x6F,0xFD,0x12,0x47,0x7F,0xC3,0xAB,0x81,0xA4,
		0x60,0x97,0x9F,0x96,0x18,0xA6,0x33,0x32,0x6C,0xF1,0x42,0x5A,
		0x66,0x0D,0x69,0x9C,0x8C,0x40,0xD8,0x98,0x55,0x38,0x6B,0x92,
		0x0D,0x62,0x5A,0x58,0xCD,0xC7,0x01,0x6C,0xCD,0x9B,0x41,0x00,
		0x68,0x68,0x3D,0xFB,
		};
	static unsigned char dh2048_g[]={
		0x02,
		};
	DH *dh;

	if ((dh=DH_new()) == NULL) return(NULL);
	dh->p=BN_bin2bn(dh2048_p,sizeof(dh2048_p),NULL);
	dh->g=BN_bin2bn(dh2048_g,sizeof(dh2048_g),NULL);
	if ((dh->p == NULL) || (dh->g == NULL))
		{ DH_free(dh); return(NULL); }
	return(dh);
	}
-----BEGIN DH PARAMETERS-----
MIIBCAKCAQEA0kX6v05rgEpuvVb0UH6reB23ziB52KiV4xlTWI+o9lr/AM+TYGnB
EPyLZmR9Yy9yg4h7RTYFsh+PtuznHyKUr69HzLuKK/Tv9JN/1WMJgCzbADl2VOZ9
Yry6AJgAMXdd+7wGRgdGa0xXrj8ximyifoFMAPlsxTkDGCw7nyjOh2pSonySylyg
Y4TQQtThu3qt4Le8zAdkJkUCkqLomVr4uErTsa3YAamAsT0UvmNWhTfhs1hZDzqd
Xr91nct/+Cxdv+GYiXf0ssq6RqEFkm49b/0SR3/Dq4GkYJeflhimMzJs8UJaZg1p
nIxA2JhVOGuSDWJaWM3HAWzNm0EAaGg9+wIBAg==
-----END DH PARAMETERS-----
#ifndef HEADER_DH_H
#include <openssl/dh.h>
#endif
DH *get_dh4096()
	{
	static unsigned char dh4096_p[]={
		0xEA,0xD7,0xE5,0x71,0x72,0x7E,0x54,0x24,0x1E,0x0A,0xA8,0x3A,
		0xF2,0xFF,0x6B,0x65,0x29,0xD1,0xBE,0xC1,0x38,0xE0,0x3E,0x00,
		0x85,0xF1,0x79,0x1A,0x1E,0x8B,0x45,0xF7,0xA7,0xA8,0xCD,0x4B,
		0x04,0xE4,0xE6,0x42,0x05,0x7F,0xE2,0xB4,0xC0,0x14,0xE6,0xFA,
		0x7B,0xB6,0xE7,0x9E,0x9E,0x07,0x33,0xD3,0xAC,0x26,0xA6,0xF6,
		0xE7,0x2E,0x04,0xC3,0x93,0x4A,0xBB,0xE4,0x5D,0x09,0x1A,0x87,
		0x46,0xDF,0x97,0xB3,0xAB,0x2A,0x38,0x32,0x93,0x07,0x7E,0xD4,
		0xBC,0x61,0x39,0x8F,0x01,0x00,0x61,0x90,0xB3,0xE4,0x28,0x4C,
		0xCB,0x0F,0xEF,0x6E,0xB0,0x04,0x7B,0xE8,0x03,0xD5,0x51,0x20,
		0xEB,0x98,0xEA,0xE6,0x5B,0x0D,0x8F,0xE4,0x12,0x52,0xB0,0x50,
		0xDE,0x03,0xAB,0xE4,0x7A,0x1C,0x98,0x08,0xDD,0xB9,0xAA,0x51,
		0x57,0x39,0xCD,0x47,0x4E,0x06,0xBC,0x24,0x38,0xC8,0xB1,0x39,
		0xA6,0x2C,0x66,0x52,0x9C,0x63,0xF8,0x21,0x17,0x1B,0x7F,0x61,
		0xA7,0xB1,0xA9,0x39,0x4E,0x00,0xF2,0x4B,0x03,0x49,0x85,0x34,
		0x21,0x72,0x86,0xC5,0xE7,0x43,0xD4,0xF3,0x56,0x0A,0x56,0x5C,
		0x02,0x92,0xB7,0x74,0x0D,0xB5,0xC8,0xA1,0x4E,0xE6,0x15,0x66,
		0xF3,0x3A,0x4B,0x07,0xEC,0x04,0x5A,0x3F,0xFA,0xBB,0xA3,0x75,
		0x4A,0x48,0xCE,0xC4,0xFD,0xD5,0x44,0x32,0x4A,0xE3,0x4C,0x09,
		0x1C,0x77,0x5E,0x6D,0xC6,0xB9,0x14,0x00,0x51,0xF0,0x80,0x8C,
		0x4C,0x14,0xCA,0x78,0xD9,0x59,0xA7,0x5D,0x76,0x81,0x15,0x79,
		0x94,0x25,0x73,0x59,0xBA,0x00,0xCA,0x80,0xEC,0x4B,0x5A,0x71,
		0x4C,0xAA,0xF4,0x3A,0x7A,0xE2,0xBF,0x40,0x57,0xC4,0x59,0xA6,
		0xBB,0x8C,0x4D,0xBF,0x7B,0xE1,0xDA,0x75,0x31,0xA5,0x89,0x13,
		0x01,0x0F,0xF3,0xE8,0x47,0x1F,0x92,0xFE,0x98,0x37,0x68,0x4F,
		0x98,0xCA,0xDB,0x8A,0x05,0xB8,0x90,0xF1,0x94,0x2E,0xDF,0xD5,
		0x32,0x2A,0x4E,0xCD,0x2B,0x6A,0x49,0x1D,0x7B,0x90,0xBF,0xC5,
		0xDE,0x75,0xF1,0x58,0xCA,0xA1,0xD7,0xBC,0x4E,0xD1,0xE1,0xE0,
		0xD4,0x10,0x2E,0xB0,0x85,0x7A,0x6D,0xB0,0xA2,0xF5,0x67,0xF2,
		0xA8,0x5E,0x4B,0xF4,0x40,0x74,0x66,0xF7,0xD5,0xEA,0x42,0xEB,
		0xD3,0x1B,0x71,0xB5,0x62,0xFE,0x1E,0x2A,0xE6,0xA8,0x94,0xAB,
		0xA8,0x4E,0xC0,0xF1,0x4B,0x3B,0xC1,0xFE,0x9C,0x89,0x2E,0x46,
		0x50,0xE9,0x91,0xAE,0x5D,0x72,0x2E,0xC7,0xC3,0xE6,0x06,0x02,
		0xA7,0xCF,0x1D,0xB5,0x4C,0xA1,0xFC,0x0B,0x8F,0x60,0x43,0xE6,
		0x5F,0xFB,0x3D,0x0C,0x36,0xDA,0x98,0x33,0x25,0xF0,0xD4,0xC3,
		0x70,0x1E,0x0C,0x9B,0xBF,0x60,0xAA,0x06,0x82,0x40,0x62,0x36,
		0xCA,0xD6,0x6B,0x9F,0x4E,0x92,0xE8,0xA2,0x38,0x00,0xAF,0xE1,
		0x36,0xD5,0x34,0x22,0x27,0x0F,0x88,0x94,0xA2,0xAA,0x8A,0xB3,
		0x21,0xEA,0xCF,0xFF,0x35,0x1A,0x7B,0xC1,0x7D,0x81,0xE7,0xEC,
		0xA7,0x2C,0x12,0x56,0x8B,0xC8,0xED,0x1D,0x7A,0x20,0x96,0xFD,
		0xD0,0x79,0x5A,0xEC,0xA8,0x3D,0xDD,0x73,0xE8,0x71,0x37,0x74,
		0xAE,0xF1,0x4D,0x59,0xAA,0x0B,0x69,0xD7,0xC7,0xA2,0x2C,0x14,
		0x9B,0x46,0x57,0x64,0xEE,0x7F,0xFF,0x53,0xF7,0x23,0x77,0xF3,
		0xE8,0x52,0x0A,0xB0,0x37,0x23,0x42,0x13,
		};
	static unsigned char dh4096_g[]={
		0x02,
		};
	DH *dh;

	if ((dh=DH_new()) == NULL) return(NULL);
	dh->p=BN_bin2bn(dh4096_p,sizeof(dh4096_p),NULL);
	dh->g=BN_bin2bn(dh4096_g,sizeof(dh4096_g),NULL);
	if ((dh->p == NULL) || (dh->g == NULL))
		{ DH_free(dh); return(NULL); }
	return(dh);
	}
-----BEGIN DH PARAMETERS-----
MIICCAKCAgEA6tflcXJ+VCQeCqg68v9rZSnRvsE44D4AhfF5Gh6LRfenqM1LBOTm
QgV/4rTAFOb6e7bnnp4HM9OsJqb25y4Ew5NKu+RdCRqHRt+Xs6sqODKTB37UvGE5
jwEAYZCz5ChMyw/vbrAEe+gD1VEg65jq5lsNj+QSUrBQ3gOr5HocmAjduapRVznN
R04GvCQ4yLE5pixmUpxj+CEXG39hp7GpOU4A8ksDSYU0IXKGxedD1PNWClZcApK3
dA21yKFO5hVm8zpLB+wEWj/6u6N1SkjOxP3VRDJK40wJHHdebca5FABR8ICMTBTK
eNlZp112gRV5lCVzWboAyoDsS1pxTKr0Onriv0BXxFmmu4xNv3vh2nUxpYkTAQ/z
6Ecfkv6YN2hPmMrbigW4kPGULt/VMipOzStqSR17kL/F3nXxWMqh17xO0eHg1BAu
sIV6bbCi9WfyqF5L9EB0ZvfV6kLr0xtxtWL+HirmqJSrqE7A8Us7wf6ciS5GUOmR
rl1yLsfD5gYCp88dtUyh/AuPYEPmX/s9DDbamDMl8NTDcB4Mm79gqgaCQGI2ytZr
n06S6KI4AK/hNtU0IicPiJSiqoqzIerP/zUae8F9gefspywSVovI7R16IJb90Hla
7Kg93XPocTd0rvFNWaoLadfHoiwUm0ZXZO5//1P3I3fz6FIKsDcjQhMCAQI=
-----END DH PARAMETERS-----